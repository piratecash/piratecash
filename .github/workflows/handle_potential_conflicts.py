#!/usr/bin/env python3
# Copyright (c) 2022-2026 The Dash Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Validate candidate PR conflicts and maintain advisory PR comments.

Usage:
    $ ./handle_potential_conflicts.py --pr-number <number>

The overlap pass only finds same-base PRs that touch the same files. This
script checks whether each candidate PR can still merge after the current PR is
pre-merged, then updates one managed comment per affected PR.
"""

from __future__ import annotations

import argparse
import base64
import html
import json
import os
import sys
from dataclasses import dataclass
from typing import Any
from urllib.parse import quote

import requests


COMMENT_MARKER = "dash-potential-conflicts:v1"
COMMENT_START = f"<!-- {COMMENT_MARKER}"
COMMENT_END = "-->"
TRUSTED_COMMENT_AUTHORS = {"github-actions[bot]"}
MAX_FILE_DETAILS = 12


@dataclass
class ManagedComment:
    comment_id: int | None
    body: str
    state: dict[str, Any]


def github_headers() -> dict[str, str]:
    headers = {
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
    }

    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    if token:
        headers["Authorization"] = f"Bearer {token}"

    return headers


def repo_name() -> str:
    return os.environ.get("GITHUB_REPOSITORY", "piratecash/piratecash")


def api_url(path: str) -> str:
    return f"https://api.github.com/repos/{repo_name()}/{path.lstrip('/')}"


def github_get(path_or_url: str) -> Any:
    url = path_or_url if path_or_url.startswith("https://") else api_url(path_or_url)
    response = requests.get(url, headers=github_headers(), timeout=30)
    response.raise_for_status()
    return response.json()


def github_get_paginated(path: str) -> list[dict[str, Any]]:
    url = api_url(path)
    items = []

    while url:
        response = requests.get(url, headers=github_headers(), timeout=30)
        response.raise_for_status()
        page = response.json()
        if not isinstance(page, list):
            raise ValueError(f"Expected a list response from {url}")
        items.extend(page)
        url = response.links.get("next", {}).get("url")

    return items


def github_patch(path: str, payload: dict[str, Any]) -> None:
    response = requests.patch(api_url(path), headers=github_headers(), json=payload, timeout=30)
    response.raise_for_status()


def github_post(path: str, payload: dict[str, Any]) -> None:
    response = requests.post(api_url(path), headers=github_headers(), json=payload, timeout=30)
    response.raise_for_status()


def github_delete(path: str) -> None:
    response = requests.delete(api_url(path), headers=github_headers(), timeout=30)
    response.raise_for_status()


def normalize_state_item(item: Any) -> dict[str, Any] | None:
    if not isinstance(item, dict):
        return None

    try:
        number = int(item["number"])
    except (KeyError, TypeError, ValueError):
        return None

    files = item.get("files", [])
    if not isinstance(files, list):
        files = []

    return {
        "number": number,
        "title": " ".join(str(item.get("title", "Unknown title")).split()),
        "url": str(item.get("url") or f"https://github.com/{repo_name()}/pull/{number}"),
        "files": [str(filename) for filename in files],
    }


def normalize_state(state: Any) -> dict[str, Any]:
    if not isinstance(state, dict):
        state = {}

    outbound = state.get("outbound", [])
    inbound = state.get("inbound", {})

    if not isinstance(outbound, list):
        outbound = []
    if not isinstance(inbound, dict):
        inbound = {}

    normalized_outbound = []
    for item in outbound:
        normalized_item = normalize_state_item(item)
        if normalized_item is not None:
            normalized_outbound.append(normalized_item)

    normalized_inbound = {}
    for key, value in inbound.items():
        normalized_item = normalize_state_item(value)
        if normalized_item is not None:
            normalized_inbound[str(key)] = normalized_item

    return {
        "outbound": normalized_outbound,
        "inbound": normalized_inbound,
    }


def extract_state(body: str) -> dict[str, Any]:
    # The state block is rendered last, but comments written before that change
    # start with it. Anchor on the last marker unless the body opens with one,
    # so marker-like text in advisory content cannot shadow the real block.
    marker_index = 0 if body.startswith(COMMENT_START) else body.rfind(COMMENT_START)
    if marker_index == -1:
        return normalize_state({})

    json_start = body.find("\n", marker_index)
    marker_end = body.find(COMMENT_END, marker_index)
    if json_start == -1 or marker_end == -1 or json_start > marker_end:
        return normalize_state({})

    encoded_state = body[json_start:marker_end].strip()
    if not encoded_state:
        return normalize_state({})

    try:
        raw_state = base64.b64decode(encoded_state).decode("utf8")
        return normalize_state(json.loads(raw_state))
    except (ValueError, json.JSONDecodeError):
        print("Warning: managed conflict comment has invalid state; replacing it", file=sys.stderr)
        return normalize_state({})


def state_is_empty(state: dict[str, Any]) -> bool:
    normalized = normalize_state(state)
    return not normalized["outbound"] and not normalized["inbound"]


def compact_title(title: str) -> str:
    return " ".join(str(title).split())


def escape_markdown_link_text(text: str) -> str:
    return str(text).replace("\\", "\\\\").replace("[", "\\[").replace("]", "\\]")


def format_code_span(text: str) -> str:
    return f"<code>{html.escape(str(text), quote=False)}</code>"


def format_files(files: list[str]) -> str:
    if not files:
        return ""

    visible_files = files[:MAX_FILE_DETAILS]
    formatted = ", ".join(format_code_span(filename) for filename in visible_files)
    remaining = len(files) - len(visible_files)
    if remaining > 0:
        formatted = f"{formatted}, and {remaining} more"

    return f" Changed files: {formatted}."


def format_pr_line(item: dict[str, Any]) -> str:
    number = int(item["number"])
    title = escape_markdown_link_text(compact_title(item.get("title", "Unknown title")))
    url = item.get("url") or f"https://github.com/{repo_name()}/pull/{number}"
    files = [str(filename) for filename in item.get("files", [])]
    return f"- [#{number}: {title}]({url}){format_files(files)}"


def render_comment_body(state: dict[str, Any]) -> str:
    state = normalize_state(state)
    state_json = json.dumps(state, sort_keys=True, separators=(",", ":"))
    state_encoded = base64.b64encode(state_json.encode("utf8")).decode("ascii")
    lines = [
        "## Potential PR merge conflicts",
        "",
        "This is advisory only. It does not block CI, but it marks PRs that will likely need a rebase depending on merge order.",
    ]

    if state["outbound"]:
        lines.extend([
            "",
            "### If this PR merges first",
            "",
            "These open PRs will likely need a rebase:",
            "",
        ])
        lines.extend(format_pr_line(item) for item in sorted(state["outbound"], key=lambda item: int(item["number"])))

    if state["inbound"]:
        lines.extend([
            "",
            "### If these PRs merge first",
            "",
            "This PR will likely need a rebase:",
            "",
        ])
        inbound_items = sorted(state["inbound"].values(), key=lambda item: int(item["number"]))
        lines.extend(format_pr_line(item) for item in inbound_items)

    # The state block goes last so notification previews start with the advisory text.
    lines.extend([
        "",
        COMMENT_START,
        state_encoded,
        COMMENT_END,
    ])

    return "\n".join(lines).rstrip() + "\n"


def get_pr_json(pr_num: int) -> dict[str, Any] | None:
    try:
        return github_get(f"pulls/{pr_num}")
    except requests.RequestException as e:
        print(f"Warning: Error fetching PR {pr_num}: {e}", file=sys.stderr)
        return None


def list_issue_comments(pr_num: int) -> list[dict[str, Any]]:
    return github_get_paginated(f"issues/{pr_num}/comments?per_page=100")


def is_trusted_comment_author(comment: dict[str, Any]) -> bool:
    user = comment.get("user")
    if not isinstance(user, dict):
        return False
    return user.get("login") in TRUSTED_COMMENT_AUTHORS


def find_managed_comments(pr_num: int) -> list[ManagedComment]:
    managed_comments = []

    for comment in list_issue_comments(pr_num):
        if not is_trusted_comment_author(comment):
            continue

        body = comment.get("body") or ""
        if COMMENT_START in body:
            managed_comments.append(ManagedComment(
                comment_id=comment.get("id"),
                body=body,
                state=extract_state(body),
            ))

    return managed_comments


def select_managed_comment(managed_comments: list[ManagedComment]) -> ManagedComment:
    if managed_comments:
        return managed_comments[-1]
    return ManagedComment(comment_id=None, body="", state=normalize_state({}))


def get_managed_comment(pr_num: int) -> ManagedComment:
    return select_managed_comment(find_managed_comments(pr_num))


def save_managed_comment(pr_num: int, state: dict[str, Any]) -> None:
    managed_comments = find_managed_comments(pr_num)
    managed_comment = select_managed_comment(managed_comments)
    state = normalize_state(state)

    if state_is_empty(state):
        for comment in managed_comments:
            if comment.comment_id is not None:
                github_delete(f"issues/comments/{comment.comment_id}")
        if managed_comments:
            print(f"Deleted stale conflict comments on PR #{pr_num}", file=sys.stderr)
        return

    body = render_comment_body(state)
    if managed_comment.comment_id is None:
        github_post(f"issues/{pr_num}/comments", {"body": body})
        print(f"Created conflict comment on PR #{pr_num}", file=sys.stderr)
    elif managed_comment.body != body:
        github_patch(f"issues/comments/{managed_comment.comment_id}", {"body": body})
        print(f"Updated conflict comment on PR #{pr_num}", file=sys.stderr)
    else:
        print(f"Conflict comment on PR #{pr_num} is already up to date", file=sys.stderr)

    for comment in managed_comments:
        if comment.comment_id is not None and comment.comment_id != managed_comment.comment_id:
            github_delete(f"issues/comments/{comment.comment_id}")
            print(f"Deleted duplicate conflict comment on PR #{pr_num}", file=sys.stderr)


def build_pr_item(pr_json: dict[str, Any], files: list[str]) -> dict[str, Any]:
    return {
        "number": int(pr_json["number"]),
        "title": compact_title(pr_json.get("title", "Unknown title")),
        "url": pr_json.get("html_url") or f"https://github.com/{repo_name()}/pull/{pr_json['number']}",
        "files": sorted(set(str(filename) for filename in files)),
    }


def item_number(item: dict[str, Any]) -> int | None:
    try:
        return int(item["number"])
    except (KeyError, TypeError, ValueError):
        return None


def file_names_from_pr_file(file_info: dict[str, Any]) -> set[str]:
    names = set()

    filename = file_info.get("filename")
    if filename:
        names.add(str(filename))

    previous_filename = file_info.get("previous_filename")
    if previous_filename:
        names.add(str(previous_filename))

    return names


def get_pr_files(pr_num: int) -> set[str]:
    files = set()
    for file_info in github_get_paginated(f"pulls/{pr_num}/files?per_page=100"):
        files.update(file_names_from_pr_file(file_info))
    return files


def discover_conflict_candidates(our_pr_json: dict[str, Any]) -> list[dict[str, Any]]:
    """Find same-base open PRs that touch at least one of the same files."""
    our_pr_num = int(our_pr_json["number"])
    our_base = our_pr_json["base"]["ref"]
    our_files = get_pr_files(our_pr_num)

    if not our_files:
        print(f"PR #{our_pr_num} has no changed files", file=sys.stderr)
        return []

    candidates = []
    open_prs = github_get_paginated(f"pulls?state=open&base={quote(our_base, safe='')}&per_page=100")
    for pr_json in open_prs:
        conflict_pr_num = int(pr_json["number"])
        if conflict_pr_num == our_pr_num:
            continue
        if pr_json.get("draft", False):
            print(f"PR #{conflict_pr_num} is a draft. Skipping conflict check", file=sys.stderr)
            continue

        conflict_files = get_pr_files(conflict_pr_num)
        overlap = sorted(our_files & conflict_files)
        if not overlap:
            continue

        candidates.append({
            "number": conflict_pr_num,
            "files": sorted(conflict_files),
            "conflicts": overlap,
        })

    return candidates


def branches_can_merge(our_pr_label: str, conflict_pr_label: str) -> bool | None:
    merge_check_url = (
        f"https://github.com/{repo_name()}/branches/pre_mergeable/"
        f"{quote(our_pr_label, safe='')}...{quote(conflict_pr_label, safe='')}"
    )
    headers = {"User-Agent": "dash-potential-conflicts"}

    try:
        response = requests.get(merge_check_url, headers=headers, timeout=30)
        response.raise_for_status()
    except requests.RequestException as e:
        print(f"Error checking mergeability at {merge_check_url}: {e}", file=sys.stderr)
        return None

    if "These branches can be automatically merged." in response.text:
        return True
    if "Can't automatic" in response.text or "octicon octicon-x" in response.text:
        return False

    print(f"Warning: Unexpected mergeability response: {merge_check_url}", file=sys.stderr)
    return None


def validate_conflicts(candidates: list[dict[str, Any]], our_pr_json: dict[str, Any]) -> list[dict[str, Any]]:
    our_pr_label = our_pr_json["head"]["label"]
    validated_conflicts = []

    for conflict in candidates:
        if "number" not in conflict:
            print("Warning: Skipping conflict entry without 'number' field", file=sys.stderr)
            continue

        conflict_pr_num = int(conflict["number"])
        conflict_pr_json = get_pr_json(conflict_pr_num)
        if conflict_pr_json is None:
            print(f"Warning: Failed to fetch PR {conflict_pr_num}, skipping", file=sys.stderr)
            continue

        if "head" not in conflict_pr_json or "label" not in conflict_pr_json["head"]:
            print(f"Warning: Invalid PR data structure for PR {conflict_pr_num}, skipping", file=sys.stderr)
            continue

        if conflict_pr_json.get("mergeable_state") == "dirty":
            print(f"PR #{conflict_pr_num} already needs rebase. Skipping conflict check", file=sys.stderr)
            continue

        if conflict_pr_json.get("draft", False):
            print(f"PR #{conflict_pr_num} is a draft. Skipping conflict check", file=sys.stderr)
            continue

        can_merge = branches_can_merge(our_pr_label, conflict_pr_json["head"]["label"])
        if can_merge is True:
            print(f"PR #{conflict_pr_num} still merges after PR #{our_pr_json['number']}", file=sys.stderr)
            continue
        if can_merge is None:
            continue

        files = conflict.get("conflicts", [])
        item = build_pr_item(conflict_pr_json, files if isinstance(files, list) else [])
        validated_conflicts.append(item)

    return validated_conflicts


def update_comments(our_pr_json: dict[str, Any], validated_conflicts: list[dict[str, Any]]) -> None:
    our_pr_num = int(our_pr_json["number"])
    our_managed_comment = get_managed_comment(our_pr_num)
    previous_outbound_numbers = {
        number
        for item in our_managed_comment.state["outbound"]
        if isinstance(item, dict)
        for number in [item_number(item)]
        if number is not None
    }
    current_outbound_numbers = {int(item["number"]) for item in validated_conflicts}

    our_state = normalize_state(our_managed_comment.state)
    our_state["outbound"] = validated_conflicts
    save_managed_comment(our_pr_num, our_state)

    targets_to_update = previous_outbound_numbers | current_outbound_numbers
    our_item_by_target = {
        int(item["number"]): build_pr_item(our_pr_json, [str(filename) for filename in item.get("files", [])])
        for item in validated_conflicts
    }

    for target_pr_num in sorted(targets_to_update):
        target_comment = get_managed_comment(target_pr_num)
        target_state = normalize_state(target_comment.state)
        inbound = target_state["inbound"]

        if target_pr_num in our_item_by_target:
            inbound[str(our_pr_num)] = our_item_by_target[target_pr_num]
        else:
            inbound.pop(str(our_pr_num), None)

        target_state["inbound"] = inbound
        save_managed_comment(target_pr_num, target_state)


def remove_outbound_reference(state: dict[str, Any], pr_num: int) -> dict[str, Any]:
    state = normalize_state(state)
    outbound = []

    for item in state["outbound"]:
        if not isinstance(item, dict):
            continue
        if item_number(item) == pr_num:
            continue
        outbound.append(item)

    state["outbound"] = outbound
    return state


def cleanup_closed_pr_comments(pr_num: int) -> None:
    managed_comment = get_managed_comment(pr_num)
    state = normalize_state(managed_comment.state)
    outbound_numbers = {
        number
        for item in state["outbound"]
        if isinstance(item, dict)
        for number in [item_number(item)]
        if number is not None
    }
    inbound_numbers = {
        int(number)
        for number in state["inbound"]
        if str(number).isdigit()
    }

    for target_pr_num in sorted(outbound_numbers):
        target_comment = get_managed_comment(target_pr_num)
        target_state = normalize_state(target_comment.state)
        target_state["inbound"].pop(str(pr_num), None)
        save_managed_comment(target_pr_num, target_state)

    for source_pr_num in sorted(inbound_numbers):
        source_comment = get_managed_comment(source_pr_num)
        source_state = remove_outbound_reference(source_comment.state, pr_num)
        save_managed_comment(source_pr_num, source_state)

    deleted_comment = False
    for comment in find_managed_comments(pr_num):
        if comment.comment_id is not None:
            github_delete(f"issues/comments/{comment.comment_id}")
            deleted_comment = True
    if deleted_comment:
        print(f"Deleted conflict comment on closed PR #{pr_num}", file=sys.stderr)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate potential PR conflicts and maintain advisory comments.",
    )
    parser.add_argument(
        "--pr-number",
        type=int,
        required=True,
        help="Pull request number to inspect.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print the current PR comment body instead of creating, updating, or deleting comments.",
    )
    parser.add_argument(
        "--cleanup-closed",
        action="store_true",
        help="Remove advisory state for a PR that has been closed.",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    our_pr_num = args.pr_number

    if args.cleanup_closed:
        if args.dry_run:
            print(f"Would clean advisory conflict comments for closed PR #{our_pr_num}")
        else:
            cleanup_closed_pr_comments(our_pr_num)
        return

    our_pr_json = get_pr_json(our_pr_num)

    if our_pr_json is None:
        print(f"Error: Failed to fetch PR {our_pr_num}", file=sys.stderr)
        sys.exit(1)

    if "head" not in our_pr_json or "label" not in our_pr_json["head"]:
        print(f"Error: Invalid PR data structure for PR {our_pr_num}", file=sys.stderr)
        sys.exit(1)

    if our_pr_json.get("draft", False):
        if args.dry_run:
            print(f"Would clean advisory conflict comments for draft PR #{our_pr_num}")
        else:
            cleanup_closed_pr_comments(our_pr_num)
        return

    candidates = discover_conflict_candidates(our_pr_json)
    validated_conflicts = validate_conflicts(candidates, our_pr_json)
    print(f"Conflicting PRs after validation: {[item['number'] for item in validated_conflicts]}", file=sys.stderr)

    if args.dry_run:
        print(render_comment_body({"outbound": validated_conflicts, "inbound": {}}))
    else:
        update_comments(our_pr_json, validated_conflicts)


if __name__ == "__main__":
    main()

package=openssl
$(package)_version=1.0.2g
$(package)_download_path=https://www.openssl.org/source
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=b784b1b3907ce39abf4098702dade6365522a253ad1552e267a9a0e89594aa33
$(package)_patches=ca.patch config-hurd.patch debian-targets.patch engines-path.patch man-dir.patch man-section.patch
$(package)_patches+=no-rpath.patch no-symbolic.patch pic.patch valgrind.patch shared-lib-ext.patch version-script.patch
$(package)_patches+=c_rehash-compat.patch block_diginotar.patch block_digicert_malaysia.patch disable_freelist.patch
$(package)_patches+=disable_sslv3_test.patch perlpath-quilt.patch no-sslv3.patch arm64-aarch64_asm.patch CVE-2016-2105.patch
$(package)_patches+=CVE-2016-2106.patch CVE-2016-2107.patch CVE-2016-2108.patch CVE-2016-2109.patch
$(package)_patches+=0b48a24ce993d1a4409d7bde26295f6df0d173cb.patch CVE-2016-2177.patch CVE-2016-2178-1.patch CVE-2016-2178-2.patch
$(package)_patches+=CVE-2016-2179.patch CVE-2016-2180.patch CVE-2016-2181-1.patch CVE-2016-2181-2.patch CVE-2016-2181-3.patch
$(package)_patches+=CVE-2016-2182.patch CVE-2016-2183.patch CVE-2016-6302.patch CVE-2016-6303.patch CVE-2016-6304.patch
$(package)_patches+=CVE-2016-6306-1.patch CVE-2016-6306-2.patch CVE-2016-2182-2.patch CVE-2016-7055.patch CVE-2016-8610.patch
$(package)_patches+=CVE-2016-8610-2.patch CVE-2017-3731.patch CVE-2017-3732.patch move-extended-feature-detection.patch fix-sha-ni.patch
$(package)_patches+=CVE-2017-3735.patch CVE-2017-3736.patch CVE-2017-3737-pre.patch CVE-2017-3737-1.patch CVE-2017-3737-2.patch
$(package)_patches+=CVE-2017-3738.patch CVE-2018-0739.patch CVE-2018-0495.patch CVE-2018-0732.patch CVE-2018-0737-1.patch
$(package)_patches+=CVE-2018-0737-2.patch CVE-2018-0737-3.patch CVE-2018-0737-4.patch CVE-2018-0734-pre1.patch CVE-2018-0734-1.patch
$(package)_patches+=CVE-2018-0734-2.patch CVE-2018-0734-3.patch CVE-2018-5407.patch CVE-2019-1559.patch CVE-2019-1547.patch
$(package)_patches+=CVE-2019-1551.patch CVE-2019-1563.patch CVE-2020-1968.patch CVE-2020-1971-1.patch CVE-2020-1971-2.patch
$(package)_patches+=CVE-2020-1971-3.patch CVE-2020-1971-4.patch CVE-2020-1971-5.patch CVE-2021-23840-pre1.patch CVE-2021-23840-pre2.patch
$(package)_patches+=CVE-2021-23840.patch CVE-2021-23841.patch

define $(package)_set_vars
$(package)_config_env=AR="$($(package)_ar)" RANLIB="$($(package)_ranlib)" CC="$($(package)_cc)"
$(package)_config_opts=--prefix=$(host_prefix) --openssldir=$(host_prefix)/etc/openssl
$(package)_config_opts+=no-capieng
$(package)_config_opts+=no-dso
$(package)_config_opts+=no-dtls1
$(package)_config_opts+=no-ec_nistp_64_gcc_128
$(package)_config_opts+=no-gost
$(package)_config_opts+=no-gmp
$(package)_config_opts+=no-heartbeats
$(package)_config_opts+=no-jpake
$(package)_config_opts+=no-krb5
$(package)_config_opts+=no-libunbound
$(package)_config_opts+=no-md2
$(package)_config_opts+=no-rc5
$(package)_config_opts+=no-rdrand
$(package)_config_opts+=no-rfc3779
$(package)_config_opts+=no-rsax
$(package)_config_opts+=no-sctp
$(package)_config_opts+=no-sha0
$(package)_config_opts+=no-shared
$(package)_config_opts+=no-ssl-trace
$(package)_config_opts+=no-ssl2
$(package)_config_opts+=no-ssl3
$(package)_config_opts+=no-static_engine
$(package)_config_opts+=no-store
$(package)_config_opts+=no-unit-test
$(package)_config_opts+=no-weak-ssl-ciphers
$(package)_config_opts+=no-zlib
$(package)_config_opts+=no-zlib-dynamic
$(package)_config_opts+=$($(package)_cflags) $($(package)_cppflags)
$(package)_config_opts_linux=-fPIC -Wa,--noexecstack
$(package)_config_opts_x86_64_linux=linux-x86_64
$(package)_config_opts_i686_linux=linux-generic32
$(package)_config_opts_arm_linux=linux-generic32
$(package)_config_opts_armv7l_linux=linux-generic32
$(package)_config_opts_aarch64_linux=linux-generic64
$(package)_config_opts_mipsel_linux=linux-generic32
$(package)_config_opts_mips_linux=linux-generic32
$(package)_config_opts_powerpc_linux=linux-generic32
$(package)_config_opts_x86_64_darwin=darwin64-x86_64-cc
$(package)_config_opts_x86_64_mingw32=mingw64
$(package)_config_opts_i686_mingw32=mingw
endef

define $(package)_preprocess_cmds
  patch -p1 < $($(package)_patch_dir)/ca.patch && \
  patch -p1 < $($(package)_patch_dir)/config-hurd.patch && \
  patch -p1 < $($(package)_patch_dir)/debian-targets.patch && \
  patch -p1 < $($(package)_patch_dir)/engines-path.patch && \
  patch -p1 < $($(package)_patch_dir)/man-dir.patch && \
  patch -p1 < $($(package)_patch_dir)/man-section.patch && \
  patch -p1 < $($(package)_patch_dir)/no-rpath.patch && \
  patch -p1 < $($(package)_patch_dir)/no-symbolic.patch && \
  patch -p1 < $($(package)_patch_dir)/pic.patch && \
  patch -p1 < $($(package)_patch_dir)/valgrind.patch && \
  patch -p1 < $($(package)_patch_dir)/shared-lib-ext.patch && \
  patch -p1 < $($(package)_patch_dir)/version-script.patch && \
  patch -p1 < $($(package)_patch_dir)/c_rehash-compat.patch && \
  patch -p1 < $($(package)_patch_dir)/block_diginotar.patch && \
  patch -p1 < $($(package)_patch_dir)/block_digicert_malaysia.patch && \
  patch -p1 < $($(package)_patch_dir)/disable_freelist.patch && \
  patch -p1 < $($(package)_patch_dir)/disable_sslv3_test.patch && \
  patch -p1 < $($(package)_patch_dir)/perlpath-quilt.patch && \
  patch -p1 < $($(package)_patch_dir)/no-sslv3.patch && \
  patch -p1 < $($(package)_patch_dir)/arm64-aarch64_asm.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2016-2105.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2016-2106.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2016-2107.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2016-2108.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2016-2109.patch && \
  patch -p1 < $($(package)_patch_dir)/0b48a24ce993d1a4409d7bde26295f6df0d173cb.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2016-2177.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2016-2178-1.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2016-2178-2.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2016-2179.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2016-2180.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2016-2181-1.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2016-2181-2.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2016-2181-3.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2016-2182.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2016-2183.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2016-6302.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2016-6303.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2016-6304.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2016-6306-1.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2016-6306-2.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2016-2182-2.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2016-7055.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2016-8610.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2016-8610-2.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2017-3731.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2017-3732.patch && \
  patch -p1 < $($(package)_patch_dir)/move-extended-feature-detection.patch && \
  patch -p1 < $($(package)_patch_dir)/fix-sha-ni.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2017-3735.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2017-3736.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2017-3737-pre.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2017-3737-1.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2017-3737-2.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2017-3738.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2018-0739.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2018-0495.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2018-0732.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2018-0737-1.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2018-0737-2.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2018-0737-3.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2018-0737-4.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2018-0734-pre1.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2018-0734-1.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2018-0734-2.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2018-0734-3.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2018-5407.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2019-1559.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2019-1547.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2019-1551.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2019-1563.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2020-1968.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2020-1971-1.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2020-1971-2.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2020-1971-3.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2020-1971-4.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2020-1971-5.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2021-23840-pre1.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2021-23840-pre2.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2021-23840.patch && \
  patch -p1 < $($(package)_patch_dir)/CVE-2021-23841.patch && \
  sed -i.old "/define DATE/d" util/mkbuildinf.pl && \
  sed -i.old "s|engines apps test|engines|" Makefile.org
endef

define $(package)_config_cmds
  ./Configure $($(package)_config_opts)
endef

define $(package)_build_cmds
  $(MAKE) -j1 build_libs libcrypto.pc libssl.pc openssl.pc
endef

define $(package)_stage_cmds
  $(MAKE) INSTALL_PREFIX=$($(package)_staging_dir) -j1 install_sw
endef

define $(package)_postprocess_cmds
  rm -rf share bin etc
endef

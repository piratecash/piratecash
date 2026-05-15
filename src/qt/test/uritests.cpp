// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/test/uritests.h>

#include <qt/guiutil.h>
#include <qt/walletmodel.h>

#include <QUrl>

void URITests::uriTests()
{
    SendCoinsRecipient rv;
    QUrl uri;
    uri.setUrl(QString("piratecash:PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug?req-dontexist="));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("piratecash:PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug?dontexist="));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug"));
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 0);

    uri.setUrl(QString("piratecash:PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug?label=Some Example Address"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug"));
    QVERIFY(rv.label == QString("Some Example Address"));
    QVERIFY(rv.amount == 0);

    uri.setUrl(QString("piratecash:PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug?amount=0.001"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug"));
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 100000);

    uri.setUrl(QString("piratecash:PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug?amount=1.001"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug"));
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 100100000);

    uri.setUrl(QString("piratecash:PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug?amount=100&label=Some Example"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug"));
    QVERIFY(rv.amount == 10000000000LL);
    QVERIFY(rv.label == QString("Some Example"));

    uri.setUrl(QString("piratecash:PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug?message=Some Example Address"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug"));
    QVERIFY(rv.label == QString());

    QVERIFY(GUIUtil::parseBitcoinURI("piratecash:PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug?message=Some Example Address", &rv));
    QVERIFY(rv.address == QString("PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug"));
    QVERIFY(rv.label == QString());

    uri.setUrl(QString("piratecash:PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug?req-message=Some Example Address"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));

    // Commas in amounts are not allowed.
    uri.setUrl(QString("piratecash:PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug?amount=1,000&label=Some Example"));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("piratecash:PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug?amount=1,000.0&label=Some Example"));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));

    // There are two amount specifications. The last value wins.
    uri.setUrl(QString("piratecash:PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug?amount=100&amount=200&label=Wikipedia Example"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug"));
    QVERIFY(rv.amount == 20000000000LL);
    QVERIFY(rv.label == QString("Wikipedia Example"));

    // The first amount value is correct. However, the second amount value is not valid. Hence, the URI is not valid.
    uri.setUrl(QString("piratecash:PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug?amount=100&amount=1,000&label=Wikipedia Example"));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));

    // Test label containing a question mark ('?').
    uri.setUrl(QString("piratecash:PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug?amount=100&label=?"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug"));
    QVERIFY(rv.amount == 10000000000LL);
    QVERIFY(rv.label == QString("?"));

    // Escape sequences are not supported.
    uri.setUrl(QString("piratecash:PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug?amount=100&label=%3F"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug"));
    QVERIFY(rv.amount == 10000000000LL);
    QVERIFY(rv.label == QString("%3F"));

    uri.setUrl(QString("piratecash:PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug?amount=100&label=Some Example&message=Some Example Message"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug"));
    QVERIFY(rv.amount == 10000000000LL);
    QVERIFY(rv.label == QString("Some Example"));
    QVERIFY(rv.message == QString("Some Example Message"));

    // Verify that IS=xxx does not lead to an error (we ignore the field)
    uri.setUrl(QString("piratecash:PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug?IS=1"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("piratecash:PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug?req-IS=1"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("piratecash:PB2vfGqfagNb12DyYTZBYWGnreyt7E4Pug"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
}

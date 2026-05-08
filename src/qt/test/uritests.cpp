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
    uri.setUrl(QString("cosanta:Cbbp3meofT1ESU5p4d9ucXpXw9pxKCMEyi?req-dontexist="));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("cosanta:Cbbp3meofT1ESU5p4d9ucXpXw9pxKCMEyi?dontexist="));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("Cbbp3meofT1ESU5p4d9ucXpXw9pxKCMEyi"));
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 0);

    uri.setUrl(QString("cosanta:Cbbp3meofT1ESU5p4d9ucXpXw9pxKCMEyi?label=Some Example Address"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("Cbbp3meofT1ESU5p4d9ucXpXw9pxKCMEyi"));
    QVERIFY(rv.label == QString("Some Example Address"));
    QVERIFY(rv.amount == 0);

    uri.setUrl(QString("cosanta:Cbbp3meofT1ESU5p4d9ucXpXw9pxKCMEyi?amount=0.001"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("Cbbp3meofT1ESU5p4d9ucXpXw9pxKCMEyi"));
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 100000);

    uri.setUrl(QString("cosanta:Cbbp3meofT1ESU5p4d9ucXpXw9pxKCMEyi?amount=1.001"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("Cbbp3meofT1ESU5p4d9ucXpXw9pxKCMEyi"));
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 100100000);

    uri.setUrl(QString("cosanta:Cbbp3meofT1ESU5p4d9ucXpXw9pxKCMEyi?amount=100&label=Some Example"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("Cbbp3meofT1ESU5p4d9ucXpXw9pxKCMEyi"));
    QVERIFY(rv.amount == 10000000000LL);
    QVERIFY(rv.label == QString("Some Example"));

    uri.setUrl(QString("cosanta:Cbbp3meofT1ESU5p4d9ucXpXw9pxKCMEyi?message=Some Example Address"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("Cbbp3meofT1ESU5p4d9ucXpXw9pxKCMEyi"));
    QVERIFY(rv.label == QString());

    QVERIFY(GUIUtil::parseBitcoinURI("cosanta:Cbbp3meofT1ESU5p4d9ucXpXw9pxKCMEyi?message=Some Example Address", &rv));
    QVERIFY(rv.address == QString("Cbbp3meofT1ESU5p4d9ucXpXw9pxKCMEyi"));
    QVERIFY(rv.label == QString());

    uri.setUrl(QString("cosanta:Cbbp3meofT1ESU5p4d9ucXpXw9pxKCMEyi?req-message=Some Example Address"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("cosanta:Cbbp3meofT1ESU5p4d9ucXpXw9pxKCMEyi?amount=1,000&label=Some Example"));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("cosanta:Cbbp3meofT1ESU5p4d9ucXpXw9pxKCMEyi?amount=1,000.0&label=Some Example"));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("cosanta:Cbbp3meofT1ESU5p4d9ucXpXw9pxKCMEyi?amount=100&label=Some Example&message=Some Example Message"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("Cbbp3meofT1ESU5p4d9ucXpXw9pxKCMEyi"));
    QVERIFY(rv.amount == 10000000000LL);
    QVERIFY(rv.label == QString("Some Example"));
    QVERIFY(rv.message == QString("Some Example Message"));

    // Verify that IS=xxx does not lead to an error (we ignore the field)
    uri.setUrl(QString("cosanta:Cbbp3meofT1ESU5p4d9ucXpXw9pxKCMEyi?IS=1"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("cosanta:Cbbp3meofT1ESU5p4d9ucXpXw9pxKCMEyi?req-IS=1"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("cosanta:Cbbp3meofT1ESU5p4d9ucXpXw9pxKCMEyi"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
}

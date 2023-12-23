// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/qrimagewidget.h>

#include <qt/guiutil.h>

#include <QApplication>
#include <QClipboard>
#include <QDrag>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>

#if defined(HAVE_CONFIG_H)
#include <config/piratecash-config.h> /* for USE_QRCODE */
#endif

#ifdef USE_QRCODE
#include <qrencode.h>
#endif

QRImageWidget::QRImageWidget(QWidget *parent):
    QLabel(parent), contextMenu(nullptr)
{
    contextMenu = new QMenu(this);
    QAction *saveImageAction = new QAction(tr("&Save Image..."), this);
    connect(saveImageAction, &QAction::triggered, this, &QRImageWidget::saveImage);
    contextMenu->addAction(saveImageAction);
    QAction *copyImageAction = new QAction(tr("&Copy Image"), this);
    connect(copyImageAction, &QAction::triggered, this, &QRImageWidget::copyImage);
    contextMenu->addAction(copyImageAction);
}

bool QRImageWidget::setQR(const QString& data, const QString& text)
{
#ifdef USE_QRCODE
    setText("");
    if (data.isEmpty()) return false;

    // limit length
    if (data.length() > MAX_URI_LENGTH) {
        setText(tr("Resulting URI too long, try to reduce the text for label / message."));
        return false;
    }

    QRcode *code = QRcode_encodeString(data.toUtf8().constData(), 0, QR_ECLEVEL_L, QR_MODE_8, 1);

    if (!code) {
        setText(tr("Error encoding URI into QR Code."));
        return false;
    }

    QImage qrImage = QImage(code->width + 6, code->width + 6, QImage::Format_RGB32);
    qrImage.fill(GUIUtil::getThemedQColor(GUIUtil::ThemedColor::BACKGROUND_WIDGET));
    unsigned char *p = code->data;
    for (int y = 0; y < code->width; y++)
    {
        for (int x = 0; x < code->width; x++)
        {
            qrImage.setPixel(x + 3, y + 3, ((*p & 1) ? GUIUtil::getThemedQColor(GUIUtil::ThemedColor::QR_PIXEL).rgb() : GUIUtil::getThemedQColor(GUIUtil::ThemedColor::BACKGROUND_WIDGET).rgb()));
            p++;
        }
    }
    QRcode_free(code);

    // Create the image with respect to the device pixel ratio
    int qrAddrImageWidth = QR_IMAGE_SIZE;
    int qrAddrImageHeight = QR_IMAGE_SIZE + 20;
    qreal scale = qApp->devicePixelRatio();
    QImage qrAddrImage = QImage(qrAddrImageWidth * scale, qrAddrImageHeight * scale, QImage::Format_RGB32);
    qrAddrImage.setDevicePixelRatio(scale);
    QPainter painter(&qrAddrImage);

    // Fill the whole image with border color
    qrAddrImage.fill(GUIUtil::getThemedQColor(GUIUtil::ThemedColor::BORDER_WIDGET));

    // Create a 2px/2px smaller rect and fill it with background color to keep the 1px border with the border color
    QRect paddedRect = QRect(1, 1, qrAddrImageWidth - 2, qrAddrImageHeight - 2);
    painter.fillRect(paddedRect, GUIUtil::getThemedQColor(GUIUtil::ThemedColor::BACKGROUND_WIDGET));
    painter.drawImage(2, 2, qrImage.scaled(QR_IMAGE_SIZE - 4, QR_IMAGE_SIZE - 4));

    // calculate ideal font size
    QFont font = GUIUtil::getFontNormal();
    qreal font_size = GUIUtil::calculateIdealFontSize((paddedRect.width() - 20), text, font);
    font.setPointSizeF(font_size);

    // paint the address
    painter.setFont(font);
    painter.setPen(GUIUtil::getThemedQColor(GUIUtil::ThemedColor::QR_PIXEL));
    paddedRect.setHeight(QR_IMAGE_SIZE + 3);
    painter.drawText(paddedRect, Qt::AlignBottom|Qt::AlignCenter, text);
    painter.end();

    setPixmap(QPixmap::fromImage(qrAddrImage));

    return true;
#else
    setText(tr("QR code support not available."));
    return false;
#endif
}

QImage QRImageWidget::exportImage()
{
    if(!pixmap())
        return QImage();
    return pixmap()->toImage();
}

void QRImageWidget::mousePressEvent(QMouseEvent *event)
{
    if(event->button() == Qt::LeftButton && pixmap())
    {
        event->accept();
        QMimeData *mimeData = new QMimeData;
        mimeData->setImageData(exportImage());

        QDrag *drag = new QDrag(this);
        drag->setMimeData(mimeData);
        drag->exec();
    } else {
        QLabel::mousePressEvent(event);
    }
}

void QRImageWidget::saveImage()
{
    if(!pixmap())
        return;
    QString fn = GUIUtil::getSaveFileName(this, tr("Save QR Code"), QString(), tr("PNG Image (*.png)"), nullptr);
    if (!fn.isEmpty())
    {
        exportImage().save(fn);
    }
}

void QRImageWidget::copyImage()
{
    if(!pixmap())
        return;
    QApplication::clipboard()->setImage(exportImage());
}

void QRImageWidget::contextMenuEvent(QContextMenuEvent *event)
{
    if(!pixmap())
        return;
    contextMenu->exec(event->globalPos());
}

#include "aboutdialog.h"
#include "ui_aboutdialog.h"
#include "clientmodel.h"

#include "init.h"
#include "version.h"

#include <QRegExp>

AboutDialog::AboutDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AboutDialog)
{
    ui->setupUi(this);
}

void AboutDialog::setModel(ClientModel *model)
{
    if(model)
    {
        QString version = tr("PirateCash Core") + " " + tr("version") + " " + model->formatFullVersion();
        /* On x86 add a bit specifier to the version so that users can distinguish between
         * 32 and 64 bit builds. On other architectures, 32/64 bit may be more ambigious.
         */
#if defined(__x86_64__)
        version += " " + tr("(%1-bit)").arg(64);
#elif defined(__i386__ )
        version += " " + tr("(%1-bit)").arg(32);
#endif
        /// HTML-format the license message from the core
        QString licenseInfo = QString::fromStdString(LicenseInfo());
        // Make URLs clickable
        QRegExp uri("<(.*)>", Qt::CaseSensitive, QRegExp::RegExp2);
        uri.setMinimal(true); // use non-greedy matching
        licenseInfo = licenseInfo.replace(uri, "<a href=\"\\1\">\\1</a>");
        // Replace newlines with HTML breaks
        licenseInfo = licenseInfo.replace("\n\n", "<br><br>");

        ui->versionLabel->setText(version + "<br><br>" + licenseInfo);
    }
}

AboutDialog::~AboutDialog()
{
    delete ui;
}

void AboutDialog::on_buttonBox_accepted()
{
    close();
}

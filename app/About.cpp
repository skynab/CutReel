// The About box: what this is, whose work is in it, and under what terms.

#include "About.h"

#include <QDialogButtonBox>
#include <QFont>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QTextBrowser>
#include <QVBoxLayout>

#include "zaro/Notices.h"
#include "zaro/Version.h"

namespace zaro::app {
namespace {

QString fromView(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

/// Where the source for this build is.
///
/// Not kSupportUrl: that one is an invitation and this one is an obligation,
/// and they would go to the same place today only by coincidence.
const QString kSourceUrl = "https://github.com/skynab/CutReel";

}  // namespace

About::About(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QString("About %1").arg(fromView(kAppName)));
    setModal(true);

    auto* column = new QVBoxLayout(this);
    column->setSpacing(12);

    // The icon at the size it was drawn at, rather than any of the others
    // scaled: 64 is the one that exists.
    //
    // The branding resource is registered by the executable and not by this
    // library -- see app/CMakeLists.txt for why -- so in the GUI tests, which
    // link the library and not the executable, there is no pixmap to be had.
    // Hidden rather than shown empty: a blank 64 pixels reads as a failed load,
    // which is exactly what it would be anywhere it mattered.
    const QPixmap mark = QIcon(":/branding/CutReel-64.png").pixmap(64, 64);
    if (!mark.isNull()) {
        auto* markLabel = new QLabel(this);
        markLabel->setPixmap(mark);
        markLabel->setAlignment(Qt::AlignHCenter);
        column->addWidget(markLabel);
    }

    auto* name = new QLabel(QString("%1 %2").arg(fromView(kAppName), fromView(kVersion)), this);
    QFont nameFont = name->font();
    nameFont.setPointSize(nameFont.pointSize() + 4);
    nameFont.setBold(true);
    name->setFont(nameFont);
    name->setAlignment(Qt::AlignHCenter);
    column->addWidget(name);

    auto* what = new QLabel(
        "A non-linear video editor.\n"
        "C++20, Qt 6, FFmpeg, GPU compositing on Qt RHI.",
        this);
    what->setAlignment(Qt::AlignHCenter);
    column->addWidget(what);

    // The licence position, said once and plainly. Somebody reading this is
    // entitled to know both halves -- that the program's own source is
    // permissive, and that the thing they installed is not -- and leaving
    // either out makes the other misleading.
    auto* licence = new QLabel(
        QString("This build is distributed under the <b>GNU General Public License, "
                "version 3 or later</b>. CutReel's own source is Apache-2.0; the binary is "
                "GPL because it includes x264, x265 and a GPL build of FFmpeg.<br><br>"
                "Complete source for this build: <a href=\"%1\">%1</a>")
            .arg(kSourceUrl),
        this);
    licence->setWordWrap(true);
    licence->setTextFormat(Qt::RichText);
    licence->setOpenExternalLinks(true);
    column->addWidget(licence);

    // Markdown rather than a hand-built list: the notice is already written,
    // and re-laying it out here would be the second copy this whole
    // arrangement exists to avoid.
    auto* notices = new QTextBrowser(this);
    notices->setMarkdown(fromView(kThirdPartyNotices));
    notices->setOpenExternalLinks(true);
    column->addWidget(notices, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons);

    resize(620, 620);
}

}  // namespace zaro::app

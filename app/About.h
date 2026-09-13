// The About box: what this is, whose work is in it, and under what terms.
#pragma once

#include <QDialog>

namespace zaro::app {

/// What the program is, and what it is made of.
///
/// **The attribution is not written here.** It is THIRD-PARTY.md, compiled into
/// the binary through <zaro/Notices.h> and rendered straight out of it, so the
/// notice that ships and the notice in the repository are the same text rather
/// than two copies that agree for as long as somebody keeps them agreeing.
///
/// It is also the source offer. GPL-3.0 §6(d) asks that anyone handed a binary
/// be told where the complete corresponding source for it is, and a person who
/// was given only an installer has nowhere else to look. That is why this is a
/// dialog with a link in it and not a paragraph in the README.
class About : public QDialog {
    Q_OBJECT

public:
    explicit About(QWidget* parent = nullptr);
};

}  // namespace zaro::app

// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/profiles.h"

#include <QTreeWidget>

namespace Glowzone::Gui
{

class Lighting;

// The overview: every fan and light with a dot in its current colour and its
// mode, split fans with one entry per LED, then the profiles.
class Sidebar : public QTreeWidget
{
    Q_OBJECT

public:
    enum Kind { Section, Fan, Led, Light, ProfileItem, NewProfile };
    enum Role {
        KindRole = Qt::UserRole + 1,
        SubtitleRole,
        DotRole,
        IndexRole,   // fan or light index in the model profile
        HeaderRole,  // LED header
        NameRole,    // profile name
        CurrentRole, // profile applied last
    };

    explicit Sidebar(Lighting *lighting, QWidget *parent);

    void refresh();
    void setProfiles(const QList<Profile> &profiles, const QString &current);
    // The applied profile reads "Name · changed" while the lighting differs from it.
    void setCurrentProfileChanged(bool changed);
    void selectFirst();

Q_SIGNALS:
    void fanSelected(int fanIndex, int header); // header -1: the whole fan
    void lightSelected(int lightIndex);
    void profileActivated(const QString &name);
    void newProfileRequested();
    void renameRequested(const QString &name);
    void deleteRequested(const QString &name);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void itemSelected(QTreeWidgetItem *item);
    void itemTriggered(QTreeWidgetItem *item);
    void contextMenu(QPoint pos);
    QTreeWidgetItem *addSection(const QString &title);
    void fitWidth();

    Lighting *m_lighting;
    QList<QTreeWidgetItem *> m_fanItems;
    QList<QTreeWidgetItem *> m_lightItems;
    QTreeWidgetItem *m_profilesSection = nullptr;
};

} // namespace Glowzone::Gui

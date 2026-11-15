// SPDX-FileCopyrightText: 2026 Meluxon
// SPDX-License-Identifier: GPL-2.0-or-later
#include "sidebar.h"
#include "lighting.h"
#include "pages.h"

#include "core/eclights.h"

#include <KLocalizedString>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>

namespace Glowzone::Gui
{

using namespace Ite8297;

// --- dots ---------------------------------------------------------------------
// An entry's dot: one part, or two halves for a split double fan.

enum class DotKind { Unknown, Off, Solid, Rainbow, Glow };

struct DotPart {
    DotPart(DotKind kind = DotKind::Unknown, const QColor &fill = {}, const QColor &outline = {})
        : kind(kind)
        , fill(fill)
        , outline(outline)
    {
    }
    DotKind kind;
    QColor fill;    // Solid, Glow
    QColor outline; // Glow
};
using Dot = QList<DotPart>;

static DotPart ledDot(HeaderSetting s, bool known, Rgb temperatureColour)
{
    if (!known) {
        return {DotKind::Unknown};
    }
    if (s.mode == Mode::Off) {
        return {DotKind::Off};
    }
    const ModeInfo *info = modeInfo(s.mode);
    if (info && info->followsTemperature) {
        return {DotKind::Solid, QColor(temperatureColour.r, temperatureColour.g, temperatureColour.b)};
    }
    if (info && info->usesColour) {
        return {DotKind::Solid, QColor(s.colour.r, s.colour.g, s.colour.b)};
    }
    return {DotKind::Rainbow};
}

static void paintDot(QPainter *p, const QRectF &r, const Dot &parts, const QPalette &pal)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const qreal n = qreal(parts.size());
    for (int i = 0; i < parts.size(); ++i) {
        const DotPart &part = parts.at(i);
        p->save();
        QPainterPath clip;
        clip.addRect(QRectF(r.x() + r.width() * i / n, r.y() - 1, r.width() / n, r.height() + 2));
        p->setClipPath(clip);
        const QColor outline = pal.color(QPalette::Mid);
        switch (part.kind) {
        case DotKind::Unknown:
            p->setPen(QPen(outline, 1, Qt::DotLine));
            p->setBrush(Qt::NoBrush);
            break;
        case DotKind::Off:
            p->setPen(QPen(outline, 1));
            p->setBrush(Qt::NoBrush);
            break;
        case DotKind::Solid:
            p->setPen(QPen(outline, 0.5));
            p->setBrush(part.fill);
            break;
        case DotKind::Glow:
            p->setPen(QPen(part.outline, 1.5));
            p->setBrush(part.fill);
            break;
        case DotKind::Rainbow: {
            QConicalGradient g(r.center(), 90);
            const QList<QColor> colours{Qt::red, QColor(255, 160, 0), Qt::yellow, Qt::green, QColor(0, 140, 255), QColor(150, 60, 255), Qt::red};
            for (int c = 0; c < colours.size(); ++c) {
                g.setColorAt(qreal(c) / qreal(colours.size() - 1), colours.at(c));
            }
            p->setPen(QPen(outline, 0.5));
            p->setBrush(g);
            break;
        }
        }
        p->drawEllipse(r.adjusted(0.5, 0.5, -0.5, -0.5));
        p->restore();
    }
    p->restore();
}

// --- delegate -----------------------------------------------------------------

class SidebarDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const QFontMetrics fm(option.font);
        const int kind = index.data(Sidebar::KindRole).toInt();
        const int pad = fm.height() / 3;
        switch (kind) {
        case Sidebar::Section:
            return {0, fm.height() + pad * 4};
        case Sidebar::Fan:
        case Sidebar::Light:
            return {0, fm.height() * 2 + pad * 2};
        default:
            return {0, fm.height() + pad * 2};
        }
    }

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const int kind = index.data(Sidebar::KindRole).toInt();
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QFontMetrics fm(opt.font);
        const int pad = fm.height() / 3;
        const QRect r = opt.rect.adjusted(pad * 2, 0, -pad * 2, 0);

        if (kind == Sidebar::Section) {
            QFont f = opt.font;
            f.setBold(true);
            p->save();
            p->setFont(f);
            p->setPen(opt.palette.color(QPalette::WindowText));
            const QRect textRect = r.adjusted(0, pad * 2, 0, 0);
            const QString text = index.data(Qt::DisplayRole).toString();
            p->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);
            const int x = textRect.x() + QFontMetrics(f).horizontalAdvance(text) + pad * 2;
            const int y = textRect.center().y() + 1;
            p->setPen(opt.palette.color(QPalette::Mid));
            p->drawLine(x, y, r.right(), y);
            p->restore();
            return;
        }

        // Selection and hover like the System Settings sidebar, in the colour
        // scheme's own colours for the current state (active, inactive window…):
        // selected = the highlight colour; hovered = a light wash with an outline.
        const QPalette::ColorGroup group = !(opt.state & QStyle::State_Enabled) ? QPalette::Disabled
            : (opt.state & QStyle::State_Active)                               ? QPalette::Active
                                                                               : QPalette::Inactive;
        const bool selected = opt.state & QStyle::State_Selected;
        const bool hovered = opt.state & QStyle::State_MouseOver;
        const QColor highlight = opt.palette.color(group, QPalette::Highlight);
        if (selected || hovered) {
            const QRectF box = QRectF(opt.rect).adjusted(pad + 0.5, 1.5, -pad - 0.5, -1.5);
            p->save();
            p->setRenderHint(QPainter::Antialiasing);
            if (selected) {
                p->setPen(Qt::NoPen);
                p->setBrush(highlight);
            } else {
                QColor fill = highlight;
                fill.setAlphaF(0.3);
                QColor outline = highlight;
                outline.setAlphaF(0.7);
                p->setPen(QPen(outline, 1));
                p->setBrush(fill);
            }
            p->drawRoundedRect(box, pad, pad);
            p->restore();
        }
        const QColor text = opt.palette.color(group, selected ? QPalette::HighlightedText : QPalette::Text);
        QColor muted = text;
        muted.setAlphaF(0.65);

        const QString title = index.data(Qt::DisplayRole).toString();
        const QString subtitle = index.data(Sidebar::SubtitleRole).toString();
        p->save();
        if (kind == Sidebar::Fan || kind == Sidebar::Light || kind == Sidebar::Led) {
            const int d = kind == Sidebar::Led ? fm.height() * 2 / 3 : fm.height();
            const QRectF dot(r.x(), r.center().y() - d / 2.0, d, d);
            paintDot(p, dot, index.data(Sidebar::DotRole).value<Dot>(), opt.palette);
            const QRect textRect = r.adjusted(d + pad * 2, 0, 0, 0);
            if (kind == Sidebar::Led) {
                p->setPen(text);
                p->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, title);
                p->setPen(muted);
                p->drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, subtitle);
            } else {
                p->setPen(text);
                p->drawText(textRect.adjusted(0, pad, 0, -textRect.height() / 2),
                            Qt::AlignLeft | Qt::AlignVCenter,
                            fm.elidedText(title, Qt::ElideRight, textRect.width()));
                QFont small = opt.font;
                small.setPointSizeF(small.pointSizeF() * 0.9);
                p->setFont(small);
                p->setPen(muted);
                p->drawText(textRect.adjusted(0, textRect.height() / 2, 0, -pad),
                            Qt::AlignLeft | Qt::AlignVCenter,
                            QFontMetrics(small).elidedText(subtitle, Qt::ElideRight, textRect.width()));
            }
        } else {
            // Profile or "save current": icon + text
            const int s = fm.height();
            const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
            if (!icon.isNull()) {
                icon.paint(p, QRect(r.x(), r.center().y() - s / 2, s, s));
            }
            p->setPen(kind == Sidebar::NewProfile ? muted : text);
            const QRect textRect = r.adjusted(s + pad * 2, 0, 0, 0);
            p->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(title, Qt::ElideRight, textRect.width()));
        }
        p->restore();
    }
};

// --- sidebar ------------------------------------------------------------------

Sidebar::Sidebar(Lighting *lighting, QWidget *parent)
    : QTreeWidget(parent)
    , m_lighting(lighting)
{
    setHeaderHidden(true);
    setRootIsDecorated(false);
    setFrameShape(QFrame::NoFrame);
    setItemDelegate(new SidebarDelegate(this));
    setIndentation(fontMetrics().height());
    setUniformRowHeights(false);
    setContextMenuPolicy(Qt::CustomContextMenu);
    // Focus by Tab only, like the System Settings sidebar: a mouse click mustn't
    // leave the list focused (Breeze then draws its scrollbar in the focus colour).
    setFocusPolicy(Qt::TabFocus);
    // The list sits on the view background (white in Breeze), like System Settings.
    setMouseTracking(true);
    viewport()->setAttribute(Qt::WA_Hover);

    const ModelProfile &model = lighting->model();
    addSection(i18nc("@title sidebar section", "Fans"));
    for (int i = 0; i < model.fans.size(); ++i) {
        const FanDef &fan = model.fans.at(i);
        auto *item = new QTreeWidgetItem(this, {fan.name.toString()});
        item->setData(0, KindRole, Fan);
        item->setData(0, IndexRole, i);
        if (fan.isDouble()) {
            for (const LedDef &led : fan.leds) {
                auto *child = new QTreeWidgetItem(item, {capitalised(led.name.toString())});
                child->setData(0, KindRole, Led);
                child->setData(0, IndexRole, i);
                child->setData(0, HeaderRole, int(led.header));
            }
            item->setExpanded(true);
        }
        m_fanItems << item;
    }
    if (!model.ecLights.isEmpty()) {
        addSection(i18nc("@title sidebar section", "Case Lights"));
        for (int i = 0; i < model.ecLights.size(); ++i) {
            auto *item = new QTreeWidgetItem(this, {model.ecLights.at(i).name.toString()});
            item->setData(0, KindRole, Light);
            item->setData(0, IndexRole, i);
            m_lightItems << item;
        }
    }
    m_profilesSection = addSection(i18nc("@title sidebar section", "Profiles"));
    fitWidth();

    connect(this, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        itemSelected(item);
    });
    // A click, or Enter (keyPressEvent). Not itemActivated: depending on the
    // single/double-click setting it comes on top of the click.
    connect(this, &QTreeWidget::itemClicked, this, &Sidebar::itemTriggered);
    connect(this, &QWidget::customContextMenuRequested, this, &Sidebar::contextMenu);
    refresh();
}

// Wide enough for every fixed text this model and language can show (names,
// modes, "· not detected"), within limits: German or French need a little more
// room than English, and only a very long profile name gets cut off.
void Sidebar::fitWidth()
{
    const ModelProfile &model = m_lighting->model();
    const QFontMetrics fm = fontMetrics();
    QFont smallFont = font();
    smallFont.setPointSizeF(smallFont.pointSizeF() * 0.9); // as the delegate draws subtitles
    const QFontMetrics small(smallFont);
    const int pad = fm.height() / 3; // the delegate's spacing
    const int rowOverhead = pad * 2 + fm.height() + pad * 2 + pad * 2; // margin, dot, gap, margin

    QStringList fanSubtitles{i18nc("@info fan state", "Not set yet"), i18nc("@info LEDs of a double fan set one by one", "Separately")};
    for (const ModeInfo &m : modesFor(model)) {
        fanSubtitles << m.name.toString();
    }
    int text = 0;
    for (const QString &st : std::as_const(fanSubtitles)) {
        text = std::max(text, small.horizontalAdvance(i18nc("@info %1 is the mode; the BIOS doesn't see the fan", "%1 · not detected", st)));
    }
    int longestMode = 0;
    for (const ModeInfo &m : modesFor(model)) {
        longestMode = std::max(longestMode, fm.horizontalAdvance(m.name.toString())); // LED rows use the normal font
    }
    for (const FanDef &fan : model.fans) {
        text = std::max(text, fm.horizontalAdvance(fan.name.toString()));
        // LED rows: indented, position on the left, mode on the right
        for (const LedDef &led : fan.leds) {
            if (fan.isDouble()) {
                text = std::max(text, fm.height() + fm.horizontalAdvance(capitalised(led.name.toString())) + pad * 3 + longestMode);
            }
        }
    }
    for (const EcLightDef &light : model.ecLights) {
        text = std::max(text, fm.horizontalAdvance(light.name.toString()));
    }
    for (const EcLights::ModeInfo &m : EcLights::modesFor(model)) {
        text = std::max(text, small.horizontalAdvance(m.name.toString()));
    }
    text = std::max(text, fm.horizontalAdvance(i18nc("@action", "Save Current Lighting…")));

    const int chars = fm.averageCharWidth();
    setFixedWidth(std::clamp(text + rowOverhead + pad, chars * 34, chars * 46));
}

QTreeWidgetItem *Sidebar::addSection(const QString &title)
{
    auto *item = new QTreeWidgetItem(this, {title});
    item->setData(0, KindRole, Section);
    item->setFlags(Qt::ItemIsEnabled);
    return item;
}

void Sidebar::refresh()
{
    const ModelProfile &model = m_lighting->model();
    const QString notSet = i18nc("@info fan state", "Not set yet");
    const Rgb temperature = m_lighting->cpuTemperatureColour();
    auto modeName = [](HeaderSetting s) {
        const ModeInfo *m = modeInfo(s.mode);
        return m ? m->name.toString() : QString();
    };

    for (int i = 0; i < m_fanItems.size(); ++i) {
        const FanDef &fan = model.fans.at(i);
        QTreeWidgetItem *item = m_fanItems.at(i);
        const bool linked = !fan.isDouble() || m_lighting->isLinked(fan);
        const quint8 first = fan.leds.constFirst().header;

        Dot dot;
        QString subtitle;
        if (linked) {
            dot << ledDot(m_lighting->led(first), m_lighting->isKnown(first), temperature);
            subtitle = m_lighting->isKnown(first) ? modeName(m_lighting->led(first)) : notSet;
        } else {
            for (const LedDef &led : fan.leds) {
                dot << ledDot(m_lighting->led(led.header), m_lighting->isKnown(led.header), temperature);
            }
            subtitle = i18nc("@info LEDs of a double fan set one by one", "Separately");
        }
        if (m_lighting->isUndetected(fan)) {
            subtitle = i18nc("@info %1 is the mode; the BIOS doesn't see the fan", "%1 · not detected", subtitle);
        }
        item->setData(0, DotRole, QVariant::fromValue(dot));
        item->setData(0, SubtitleRole, subtitle);

        for (int c = 0; c < item->childCount(); ++c) {
            QTreeWidgetItem *child = item->child(c);
            const quint8 header = quint8(child->data(0, HeaderRole).toInt());
            child->setHidden(linked);
            child->setData(0, DotRole, QVariant::fromValue(Dot{ledDot(m_lighting->led(header), m_lighting->isKnown(header), temperature)}));
            child->setData(0, SubtitleRole, m_lighting->isKnown(header) ? modeName(m_lighting->led(header)) : notSet);
        }
        if (linked && currentItem() && currentItem()->parent() == item) {
            setCurrentItem(item); // the LED row just disappeared
        }
    }

    for (int i = 0; i < m_lightItems.size(); ++i) {
        const EcLightDef &light = model.ecLights.at(i);
        const auto state = m_lighting->light(light.index);
        DotPart part{DotKind::Unknown};
        QString subtitle = i18nc("@info light state not read yet", "Unknown");
        if (state) {
            const EcLights::ModeInfo *m = EcLights::modeInfo(state->mode);
            subtitle = m ? m->name.toString() : subtitle;
            part = state->mode == EcLights::ModeOff ? DotPart{DotKind::Off} : DotPart{DotKind::Glow, QColor::fromRgb(light.colour), QColor::fromRgb(light.glow)};
        }
        m_lightItems.at(i)->setData(0, DotRole, QVariant::fromValue(Dot{part}));
        m_lightItems.at(i)->setData(0, SubtitleRole, subtitle);
    }
    // Showing or hiding the LED entries can drop the selection while the item
    // stays current – the page would show without a highlighted entry.
    if (QTreeWidgetItem *current = currentItem(); current && !current->isSelected() && (current->flags() & Qt::ItemIsSelectable)) {
        current->setSelected(true);
    }
    viewport()->update();
}

void Sidebar::setProfiles(const QList<Profile> &profiles, const QString &current)
{
    // Everything after the "Profiles" section heading is rebuilt.
    const int start = indexOfTopLevelItem(m_profilesSection) + 1;
    while (topLevelItemCount() > start) {
        delete takeTopLevelItem(start);
    }
    for (const Profile &p : profiles) {
        auto *item = new QTreeWidgetItem(this, {p.name});
        const bool isCurrent = p.name.compare(current, Qt::CaseInsensitive) == 0;
        item->setData(0, KindRole, ProfileItem);
        item->setData(0, NameRole, p.name);
        item->setData(0, CurrentRole, isCurrent);
        item->setData(0, Qt::DecorationRole, isCurrent ? QIcon::fromTheme(QStringLiteral("object-select-symbolic")) : QIcon());
        item->setFlags(Qt::ItemIsEnabled);
        item->setToolTip(0, isCurrent ? i18nc("@info:tooltip", "Applied last. Click to apply it again.") : i18nc("@info:tooltip", "Click to apply"));
    }
    auto *add = new QTreeWidgetItem(this, {i18nc("@action", "Save Current Lighting…")});
    add->setData(0, KindRole, NewProfile);
    add->setData(0, Qt::DecorationRole, QIcon::fromTheme(QStringLiteral("list-add")));
    add->setFlags(Qt::ItemIsEnabled);
}

void Sidebar::setCurrentProfileChanged(bool changed)
{
    for (int i = 0; i < topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = topLevelItem(i);
        if (item->data(0, KindRole).toInt() == ProfileItem && item->data(0, CurrentRole).toBool()) {
            const QString name = item->data(0, NameRole).toString();
            item->setText(0, changed ? i18nc("@item profile, %1 its name", "%1 · changed", name) : name);
            item->setToolTip(0, changed ? i18nc("@info:tooltip", "Changed since it was applied. Click to go back to it, or use Save.")
                                        : i18nc("@info:tooltip", "Applied last. Click to apply it again."));
        }
    }
    viewport()->update();
}

void Sidebar::selectFirst()
{
    if (!m_fanItems.isEmpty()) {
        setCurrentItem(m_fanItems.constFirst());
    }
}

void Sidebar::itemSelected(QTreeWidgetItem *item)
{
    if (!item) {
        return;
    }
    switch (item->data(0, KindRole).toInt()) {
    case Fan:
        Q_EMIT fanSelected(item->data(0, IndexRole).toInt(), -1);
        break;
    case Led:
        Q_EMIT fanSelected(item->data(0, IndexRole).toInt(), item->data(0, HeaderRole).toInt());
        break;
    case Light:
        Q_EMIT lightSelected(item->data(0, IndexRole).toInt());
        break;
    default:
        break;
    }
}

void Sidebar::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && currentItem()) {
        itemTriggered(currentItem());
        return;
    }
    QTreeWidget::keyPressEvent(event);
}

// Profiles and "save current" act on click/Enter and never become the
// selected page.
void Sidebar::itemTriggered(QTreeWidgetItem *item)
{
    if (!item) {
        return;
    }
    switch (item->data(0, KindRole).toInt()) {
    case ProfileItem:
        Q_EMIT profileActivated(item->data(0, NameRole).toString());
        break;
    case NewProfile:
        Q_EMIT newProfileRequested();
        break;
    default:
        break;
    }
}

void Sidebar::contextMenu(QPoint pos)
{
    QTreeWidgetItem *item = itemAt(pos);
    if (!item || item->data(0, KindRole).toInt() != ProfileItem) {
        return;
    }
    const QString name = item->data(0, NameRole).toString();
    QMenu menu(this);
    menu.addAction(QIcon::fromTheme(QStringLiteral("dialog-ok-apply")), i18nc("@action:inmenu", "Apply"), this, [this, name] {
        Q_EMIT profileActivated(name);
    });
    menu.addAction(QIcon::fromTheme(QStringLiteral("edit-rename")), i18nc("@action:inmenu", "Rename…"), this, [this, name] {
        Q_EMIT renameRequested(name);
    });
    menu.addSeparator();
    menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), i18nc("@action:inmenu", "Delete…"), this, [this, name] {
        Q_EMIT deleteRequested(name);
    });
    menu.exec(viewport()->mapToGlobal(pos));
}

} // namespace Glowzone::Gui

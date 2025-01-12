// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "aspects.h"

#include "algorithm.h"
#include "checkablemessagebox.h"
#include "environment.h"
#include "fancylineedit.h"
#include "layoutbuilder.h"
#include "pathchooser.h"
#include "qtcassert.h"
#include "qtcolorbutton.h"
#include "qtcsettings.h"
#include "utilstr.h"
#include "variablechooser.h"

#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QTextEdit>

using namespace Layouting;

namespace Utils {

static QSettings *theSettings = nullptr;

void BaseAspect::setSettings(QSettings *settings)
{
    theSettings = settings;
}

QSettings *BaseAspect::settings()
{
    return theSettings;
}

namespace Internal {

class BaseAspectPrivate
{
public:
    explicit BaseAspectPrivate(AspectContainer *container) : m_container(container) {}

    Id m_id;
    std::function<QVariant(const QVariant &)> m_toSettings;
    std::function<QVariant(const QVariant &)> m_fromSettings;

    QString m_displayName;
    QString m_settingsKey; // Name of data in settings.
    QString m_tooltip;
    QString m_labelText;
    QPixmap m_labelPixmap;
    QIcon m_icon;
    QPointer<QLabel> m_label; // Owned by configuration widget
    QPointer<QAction> m_action; // Owned by us.
    AspectContainer *m_container = nullptr; // Not owned by us.

    bool m_visible = true;
    bool m_enabled = true;
    bool m_readOnly = false;
    bool m_autoApply = true;
    int m_spanX = 1;
    int m_spanY = 1;
    BaseAspect::ConfigWidgetCreator m_configWidgetCreator;
    QList<QPointer<QWidget>> m_subWidgets;

    BaseAspect::DataCreator m_dataCreator;
    BaseAspect::DataCloner m_dataCloner;
    QList<BaseAspect::DataExtractor> m_dataExtractors;
};

} // Internal

/*!
    \class Utils::BaseAspect
    \inmodule QtCreator

    \brief The \c BaseAspect class provides a common base for classes implementing
    aspects.

    An aspect is a hunk of data like a property or collection of related
    properties of some object, together with a description of its behavior
    for common operations like visualizing or persisting.

    Simple aspects are for example a boolean property represented by a QCheckBox
    in the user interface, or a string property represented by a PathChooser,
    selecting directories in the filesystem.

    While aspects implementations usually have the ability to visualize and to persist
    their data, or use an ID, neither of these is mandatory.
*/

/*!
    Constructs a base aspect.

    If \a container is non-null, the aspect is made known to the container.
*/
BaseAspect::BaseAspect(AspectContainer *container)
    : d(new Internal::BaseAspectPrivate(container))
{
    if (container)
        container->registerAspect(this);
    addDataExtractor(this, &BaseAspect::variantValue, &Data::value);
}

/*!
    Destructs a BaseAspect.
*/
BaseAspect::~BaseAspect()
{
    delete d->m_action;
}

Id BaseAspect::id() const
{
    return d->m_id;
}

void BaseAspect::setId(Id id)
{
    d->m_id = id;
}

QVariant BaseAspect::variantValue() const
{
    return {};
}

/*!
    Sets \a value.

    Prefer the typed setValue() of derived classes.
*/
void BaseAspect::setVariantValue(const QVariant &value)
{
    Q_UNUSED(value)
    QTC_CHECK(false);
}

void BaseAspect::setDefaultVariantValue(const QVariant &value)
{
    Q_UNUSED(value)
    QTC_CHECK(false);
}

/*!
    \internal

    Sets \a value without emitting signals.

    Prefer the typed setValueQuietly() of derived classes.
*/
void BaseAspect::setVariantValueQuietly(const QVariant &value)
{
    Q_UNUSED(value)
    QTC_CHECK(false);
}

QVariant BaseAspect::defaultVariantValue() const
{
    return {};
}

/*!
    \fn TypedAspect::setDefaultValue(const ValueType &value)

    Sets a default \a value and the current value for this aspect.

    \note The current value will be set silently to the same value.
    It is reasonable to only set default values in the setup phase
    of the aspect.

    Default values will not be stored in settings.
*/

void BaseAspect::setDisplayName(const QString &displayName)
{
    d->m_displayName = displayName;
}

bool BaseAspect::isVisible() const
{
    return d->m_visible;
}

/*!
    Shows or hides the visual representation of this aspect depending
    on the value of \a visible.
    By default, it is visible.
 */
void BaseAspect::setVisible(bool visible)
{
    d->m_visible = visible;
    for (QWidget *w : std::as_const(d->m_subWidgets)) {
        QTC_ASSERT(w, continue);
        // This may happen during layout building. Explicit setting visibility here
        // may create a show a toplevel widget for a moment until it is parented
        // to some non-shown widget.
        if (!visible || w->parentWidget())
            w->setVisible(visible);
    }
}

void BaseAspect::setupLabel()
{
    QTC_ASSERT(!d->m_label, delete d->m_label);
    if (d->m_labelText.isEmpty() && d->m_labelPixmap.isNull())
        return;
    d->m_label = new QLabel(d->m_labelText);
    d->m_label->setTextInteractionFlags(d->m_label->textInteractionFlags()
                                        | Qt::TextSelectableByMouse);
    connect(d->m_label, &QLabel::linkActivated, this, [this](const QString &link) {
        emit labelLinkActivated(link);
    });
    if (!d->m_labelPixmap.isNull())
        d->m_label->setPixmap(d->m_labelPixmap);
    registerSubWidget(d->m_label);
}

void BaseAspect::addLabeledItem(LayoutItem &parent, QWidget *widget)
{
    setupLabel();
    if (QLabel *l = label()) {
        l->setBuddy(widget);
        parent.addItem(l);
        parent.addItem(Span(std::max(d->m_spanX - 1, 1), LayoutItem(widget)));
    } else {
        parent.addItem(LayoutItem(widget));
    }
}

/*!
    Sets \a labelText as text for the separate label in the visual
    representation of this aspect.
*/
void BaseAspect::setLabelText(const QString &labelText)
{
    d->m_labelText = labelText;
    if (d->m_label)
        d->m_label->setText(labelText);
}

/*!
    Sets \a labelPixmap as pixmap for the separate label in the visual
    representation of this aspect.
*/
void BaseAspect::setLabelPixmap(const QPixmap &labelPixmap)
{
    d->m_labelPixmap = labelPixmap;
    if (d->m_label)
        d->m_label->setPixmap(labelPixmap);
}

void BaseAspect::setIcon(const QIcon &icon)
{
    d->m_icon = icon;
    if (d->m_action)
        d->m_action->setIcon(icon);
}

/*!
    Returns the current text for the separate label in the visual
    representation of this aspect.
*/
QString BaseAspect::labelText() const
{
    return d->m_labelText;
}

QLabel *BaseAspect::label() const
{
    return d->m_label.data();
}

QString BaseAspect::toolTip() const
{
    return d->m_tooltip;
}

/*!
    Sets \a tooltip as tool tip for the visual representation of this aspect.
 */
void BaseAspect::setToolTip(const QString &tooltip)
{
    d->m_tooltip = tooltip;
    for (QWidget *w : std::as_const(d->m_subWidgets)) {
        QTC_ASSERT(w, continue);
        w->setToolTip(tooltip);
    }
}

bool BaseAspect::isEnabled() const
{
    return d->m_enabled;
}

void BaseAspect::setEnabled(bool enabled)
{
    d->m_enabled = enabled;
    for (QWidget *w : std::as_const(d->m_subWidgets)) {
        QTC_ASSERT(w, continue);
        w->setEnabled(enabled);
    }
}

/*!
    Makes the enabled state of this aspect depend on the checked state of \a checker.
*/
void BaseAspect::setEnabler(BoolAspect *checker)
{
    QTC_ASSERT(checker, return);
    setEnabled(checker->value());
    connect(checker, &BoolAspect::volatileValueChanged, this, [this, checker] {
        BaseAspect::setEnabled(checker->volatileValue());
    });
    connect(checker, &BoolAspect::changed, this, [this, checker] {
        BaseAspect::setEnabled(checker->volatileValue());
    });
}

bool BaseAspect::isReadOnly() const
{
    return d->m_readOnly;
}

void BaseAspect::setReadOnly(bool readOnly)
{
    d->m_readOnly = readOnly;
    for (QWidget *w : std::as_const(d->m_subWidgets)) {
        QTC_ASSERT(w, continue);
        if (auto lineEdit = qobject_cast<QLineEdit *>(w))
            lineEdit->setReadOnly(readOnly);
        else if (auto textEdit = qobject_cast<QTextEdit *>(w))
            textEdit->setReadOnly(readOnly);
        else if (auto pathChooser = qobject_cast<PathChooser *>(w))
            pathChooser->setReadOnly(readOnly);
    }
}

void BaseAspect::setSpan(int x, int y)
{
    d->m_spanX = x;
    d->m_spanY = y;
}

bool BaseAspect::isAutoApply() const
{
    return d->m_autoApply;
}

/*!
    Sets auto-apply mode. When auto-apply mode is \a on, user interaction to this
    aspect's widget will not modify the \c value of the aspect until \c apply()
    is called programmatically.

    \sa setSettingsKey()
*/

void BaseAspect::setAutoApply(bool on)
{
    d->m_autoApply = on;
}

/*!
    \internal
*/
void BaseAspect::setConfigWidgetCreator(const ConfigWidgetCreator &configWidgetCreator)
{
    d->m_configWidgetCreator = configWidgetCreator;
}

/*!
    Returns the key to be used when accessing the settings.

    \sa setSettingsKey()
*/
QString BaseAspect::settingsKey() const
{
    return d->m_settingsKey;
}

/*!
    Sets the \a key to be used when accessing the settings.

    \sa settingsKey()
*/
void BaseAspect::setSettingsKey(const QString &key)
{
    d->m_settingsKey = key;
}

/*!
    Sets the \a key and \a group to be used when accessing the settings.

    \sa settingsKey()
*/
void BaseAspect::setSettingsKey(const QString &group, const QString &key)
{
    d->m_settingsKey = group + "/" + key;
}

/*!
    Returns the string that should be used when this action appears in menus
    or other places that are typically used with Book style capitalization.

    If no display name is set, the label text will be used as fallback.
*/

QString BaseAspect::displayName() const
{
    return d->m_displayName.isEmpty() ? d->m_labelText : d->m_displayName;
}

/*!
    \internal
*/
QWidget *BaseAspect::createConfigWidget() const
{
    return d->m_configWidgetCreator ? d->m_configWidgetCreator() : nullptr;
}

QAction *BaseAspect::action()
{
    if (!d->m_action) {
        d->m_action = new QAction(labelText());
        d->m_action->setIcon(d->m_icon);
    }
    return d->m_action;
}

AspectContainer *BaseAspect::container() const
{
    return d->m_container;
}

/*!
    Adds the visual representation of this aspect to the layout with the
    specified \a parent using a layout builder.
*/
void BaseAspect::addToLayout(LayoutItem &)
{
}

void createItem(Layouting::LayoutItem *item, const BaseAspect &aspect)
{
    const_cast<BaseAspect &>(aspect).addToLayout(*item);
}

void createItem(Layouting::LayoutItem *item, const BaseAspect *aspect)
{
    const_cast<BaseAspect *>(aspect)->addToLayout(*item);
}


/*!
    Updates this aspect's value from user-initiated changes in the widget.

    Emits changed() if the value changed.
*/
void BaseAspect::apply()
{
    if (isDirty()) {
        silentApply();
        if (hasAction())
            emit action()->triggered(variantValue().toBool());
        emit changed();
    }
}

/*!
    Updates this aspect's value from user-initiated changes in the widget.

    \returns whether the value changed. Does not emit signals.
*/

void BaseAspect::silentApply()
{
    guiToBuffer();
    bufferToInternal();
    internalToExternal();
}
/*!
    Discard user changes in the widget and restore widget contents from
    aspect's value.

    This has only an effect if \c isAutoApply is false.
*/
void BaseAspect::cancel()
{
    internalToBuffer();
    bufferToGui();
}

void BaseAspect::finish()
{
    // No qDeleteAll() possible as long as the connect in registerSubWidget() exist.
    while (d->m_subWidgets.size())
        delete d->m_subWidgets.takeLast();
}

bool BaseAspect::hasAction() const
{
    return d->m_action != nullptr;
}

bool BaseAspect::isDirty()
{
    return false;
}

void BaseAspect::registerSubWidget(QWidget *widget)
{
    d->m_subWidgets.append(widget);

    // FIXME: This interferes with qDeleteAll() in finish() and destructor,
    // it would not be needed when all users actually deleted their subwidgets,
    // e.g. the SettingsPage::finish() base implementation, but this still
    // leaves the cases where no such base functionality is available, e.g.
    // in the run/build config aspects.
    connect(widget, &QObject::destroyed, this, [this, widget] {
        d->m_subWidgets.removeAll(widget);
    });

    widget->setEnabled(d->m_enabled);
    widget->setToolTip(d->m_tooltip);

    // Visible is on by default. Not setting it explicitly avoid popping
    // it up when the parent is not set yet, the normal case.
    if (!d->m_visible)
        widget->setVisible(d->m_visible);
}

void BaseAspect::saveToMap(QVariantMap &data, const QVariant &value,
                           const QVariant &defaultValue, const QString &key)
{
    if (key.isEmpty())
        return;
    if (value == defaultValue)
        data.remove(key);
    else
        data.insert(key, value);
}

/*!
    Retrieves the internal value of this BaseAspect from the QVariantMap \a map.
*/
void BaseAspect::fromMap(const QVariantMap &map)
{
    if (settingsKey().isEmpty())
        return;
    const QVariant val = map.value(settingsKey(), toSettingsValue(defaultVariantValue()));
    setVariantValue(fromSettingsValue(val));
}

/*!
    Stores the internal value of this BaseAspect into the QVariantMap \a map.
*/
void BaseAspect::toMap(QVariantMap &map) const
{
    if (settingsKey().isEmpty())
        return;
    saveToMap(map, toSettingsValue(variantValue()), toSettingsValue(defaultVariantValue()), settingsKey());
}

void BaseAspect::readSettings()
{
    if (settingsKey().isEmpty())
        return;
    const QVariant val = settings()->value(settingsKey());
    setVariantValue(val.isValid() ? fromSettingsValue(val) : defaultVariantValue());
}

void BaseAspect::writeSettings() const
{
    if (settingsKey().isEmpty())
        return;
    QtcSettings::setValueWithDefault(settings(),
                                     settingsKey(),
                                     toSettingsValue(variantValue()),
                                     toSettingsValue(defaultVariantValue()));
}

void BaseAspect::setFromSettingsTransformation(const SavedValueTransformation &transform)
{
    d->m_fromSettings = transform;
}

void BaseAspect::setToSettingsTransformation(const SavedValueTransformation &transform)
{
    d->m_toSettings = transform;
}

QVariant BaseAspect::toSettingsValue(const QVariant &val) const
{
    return d->m_toSettings ? d->m_toSettings(val) : val;
}

QVariant BaseAspect::fromSettingsValue(const QVariant &val) const
{
    return d->m_fromSettings ? d->m_fromSettings(val) : val;
}

namespace Internal {

class BoolAspectPrivate
{
public:
    BoolAspect::LabelPlacement m_labelPlacement = BoolAspect::LabelPlacement::AtCheckBox;
    QPointer<QAbstractButton> m_button; // Owned by configuration widget
    QPointer<QGroupBox> m_groupBox; // For BoolAspects handling GroupBox check boxes
    bool m_buttonIsAdopted = false;
};

class ColorAspectPrivate
{
public:
    QPointer<QtColorButton> m_colorButton; // Owned by configuration widget
};

class SelectionAspectPrivate
{
public:
    ~SelectionAspectPrivate() { delete m_buttonGroup; }

    SelectionAspect::DisplayStyle m_displayStyle
            = SelectionAspect::DisplayStyle::RadioButtons;
    QVector<SelectionAspect::Option> m_options;

    // These are all owned by the configuration widget.
    QList<QPointer<QRadioButton>> m_buttons;
    QPointer<QComboBox> m_comboBox;
    QPointer<QButtonGroup> m_buttonGroup;
};

class MultiSelectionAspectPrivate
{
public:
    explicit MultiSelectionAspectPrivate(MultiSelectionAspect *q) : q(q) {}

    bool setValueSelectedHelper(const QString &value, bool on);

    MultiSelectionAspect *q;
    QStringList m_allValues;
    MultiSelectionAspect::DisplayStyle m_displayStyle
        = MultiSelectionAspect::DisplayStyle::ListView;

    // These are all owned by the configuration widget.
    QPointer<QListWidget> m_listView;
};

class StringAspectPrivate
{
public:
    StringAspect::DisplayStyle m_displayStyle = StringAspect::LabelDisplay;
    StringAspect::CheckBoxPlacement m_checkBoxPlacement
        = StringAspect::CheckBoxPlacement::Right;
    StringAspect::UncheckedSemantics m_uncheckedSemantics
        = StringAspect::UncheckedSemantics::Disabled;
    std::function<QString(const QString &)> m_displayFilter;
    std::unique_ptr<BoolAspect> m_checker;

    Qt::TextElideMode m_elideMode = Qt::ElideNone;
    QString m_placeHolderText;
    QString m_prompDialogFilter;
    QString m_prompDialogTitle;
    QStringList m_commandVersionArguments;
    QString m_historyCompleterKey;
    PathChooser::Kind m_expectedKind = PathChooser::File;
    Environment m_environment;
    QPointer<ElidingLabel> m_labelDisplay;
    QPointer<FancyLineEdit> m_lineEditDisplay;
    QPointer<PathChooser> m_pathChooserDisplay;
    QPointer<QTextEdit> m_textEditDisplay;
    MacroExpanderProvider m_expanderProvider;
    FilePath m_baseFileName;
    StringAspect::ValueAcceptor m_valueAcceptor;
    std::optional<FancyLineEdit::ValidationFunction> m_validator;
    std::function<void()> m_openTerminal;

    bool m_undoRedoEnabled = true;
    bool m_acceptRichText = false;
    bool m_showToolTipOnLabel = false;
    bool m_fileDialogOnly = false;
    bool m_useResetButton = false;
    bool m_autoApplyOnEditingFinished = false;
    // Used to block recursive editingFinished signals for example when return is pressed, and
    // the validation changes focus by opening a dialog
    bool m_blockAutoApply = false;
    bool m_allowPathFromDevice = true;
    bool m_validatePlaceHolder = false;

    template<class Widget> void updateWidgetFromCheckStatus(StringAspect *aspect, Widget *w)
    {
        const bool enabled = !m_checker || m_checker->value();
        if (m_uncheckedSemantics == StringAspect::UncheckedSemantics::Disabled)
            w->setEnabled(enabled && aspect->isEnabled());
        else
            w->setReadOnly(!enabled || aspect->isReadOnly());
    }
};

class IntegerAspectPrivate
{
public:
    std::optional<qint64> m_minimumValue;
    std::optional<qint64> m_maximumValue;
    int m_displayIntegerBase = 10;
    qint64 m_displayScaleFactor = 1;
    QString m_prefix;
    QString m_suffix;
    QString m_specialValueText;
    int m_singleStep = 1;
    QPointer<QSpinBox> m_spinBox; // Owned by configuration widget
};

class DoubleAspectPrivate
{
public:
    std::optional<double> m_minimumValue;
    std::optional<double> m_maximumValue;
    QString m_prefix;
    QString m_suffix;
    QString m_specialValueText;
    double m_singleStep = 1;
    QPointer<QDoubleSpinBox> m_spinBox; // Owned by configuration widget
};

class StringListAspectPrivate
{
public:
};

class TextDisplayPrivate
{
public:
    QString m_message;
    InfoLabel::InfoType m_type;
    QPointer<InfoLabel> m_label;
};

} // Internal

/*!
    \enum Utils::StringAspect::DisplayStyle
    \inmodule QtCreator

    The DisplayStyle enum describes the main visual characteristics of a
    string aspect.

      \value LabelDisplay
             Based on QLabel, used for text that cannot be changed by the
             user in this place, for example names of executables that are
             defined in the build system.

      \value LineEditDisplay
             Based on QLineEdit, used for user-editable strings that usually
             fit on a line.

      \value TextEditDisplay
             Based on QTextEdit, used for user-editable strings that often
             do not fit on a line.

      \value PathChooserDisplay
             Based on Utils::PathChooser.

    \sa Utils::PathChooser
*/

/*!
    \class Utils::StringAspect
    \inmodule QtCreator

    \brief A string aspect is a string-like property of some object, together with
    a description of its behavior for common operations like visualizing or
    persisting.

    String aspects can represent for example a parameter for an external commands,
    paths in a file system, or simply strings.

    The string can be displayed using a QLabel, QLineEdit, QTextEdit or
    Utils::PathChooser.

    The visual representation often contains a label in front of the display
    of the actual value.
*/

/*!
    Constructs the string aspect \a container.
 */

StringAspect::StringAspect(AspectContainer *container)
    : TypedAspect(container), d(new Internal::StringAspectPrivate)
{
    setSpan(2, 1); // Default: Label + something

    addDataExtractor(this, &StringAspect::value, &Data::value);
    addDataExtractor(this, &StringAspect::filePath, &Data::filePath);
}

/*!
    \internal
*/
StringAspect::~StringAspect() = default;

/*!
    \internal
*/
void StringAspect::setValueAcceptor(StringAspect::ValueAcceptor &&acceptor)
{
    d->m_valueAcceptor = std::move(acceptor);
}

/*!
    Returns the value of this StringAspect as an ordinary \c QString.
*/
QString StringAspect::value() const
{
    return TypedAspect::value();
}

/*!
    \reimp
*/
void StringAspect::fromMap(const QVariantMap &map)
{
    if (!settingsKey().isEmpty())
        setValueQuietly(map.value(settingsKey(), defaultValue()).toString());
    if (d->m_checker)
        d->m_checker->fromMap(map);
}

/*!
    \reimp
*/
void StringAspect::toMap(QVariantMap &map) const
{
    saveToMap(map, value(), defaultValue(), settingsKey());
    if (d->m_checker)
        d->m_checker->toMap(map);
}

/*!
    Returns the value of this string aspect as \c Utils::FilePath.

    \note This simply uses \c FilePath::fromUserInput() for the
    conversion. It does not use any check that the value is actually
    a valid file path.
*/
FilePath StringAspect::filePath() const
{
    return FilePath::fromUserInput(value());
}

PathChooser *StringAspect::pathChooser() const
{
    return d->m_pathChooserDisplay.data();
}

/*!
    \internal
*/
void StringAspect::setShowToolTipOnLabel(bool show)
{
    d->m_showToolTipOnLabel = show;
    bufferToGui();
}

/*!
    Sets a \a displayFilter for fine-tuning the visual appearance
    of the value of this string aspect.
*/
void StringAspect::setDisplayFilter(const std::function<QString(const QString &)> &displayFilter)
{
    d->m_displayFilter = displayFilter;
}

/*!
    Returns the check box value.

    \sa makeCheckable(), setChecked()
*/
bool StringAspect::isChecked() const
{
    return !d->m_checker || d->m_checker->value();
}

/*!
    Sets the check box of this aspect to \a checked.

    \sa makeCheckable(), isChecked()
*/
void StringAspect::setChecked(bool checked)
{
    QTC_ASSERT(d->m_checker, return);
    d->m_checker->setValue(checked);
}

/*!
    Selects the main display characteristics of the aspect according to
    \a displayStyle.

    \note Not all StringAspect features are available with all display styles.

    \sa Utils::StringAspect::DisplayStyle
*/
void StringAspect::setDisplayStyle(DisplayStyle displayStyle)
{
    d->m_displayStyle = displayStyle;
}

/*!
    Sets \a placeHolderText as place holder for line and text displays.
*/
void StringAspect::setPlaceHolderText(const QString &placeHolderText)
{
    d->m_placeHolderText = placeHolderText;
    if (d->m_lineEditDisplay)
        d->m_lineEditDisplay->setPlaceholderText(placeHolderText);
    if (d->m_textEditDisplay)
        d->m_textEditDisplay->setPlaceholderText(placeHolderText);
}

void FilePathAspect::setPromptDialogFilter(const QString &filter)
{
    d->m_prompDialogFilter = filter;
    if (d->m_pathChooserDisplay)
        d->m_pathChooserDisplay->setPromptDialogFilter(filter);
}

void FilePathAspect::setPromptDialogTitle(const QString &title)
{
    d->m_prompDialogTitle = title;
    if (d->m_pathChooserDisplay)
        d->m_pathChooserDisplay->setPromptDialogTitle(title);
}

void FilePathAspect::setCommandVersionArguments(const QStringList &arguments)
{
    d->m_commandVersionArguments = arguments;
    if (d->m_pathChooserDisplay)
        d->m_pathChooserDisplay->setCommandVersionArguments(arguments);
}

void FilePathAspect::setAllowPathFromDevice(bool allowPathFromDevice)
{
    d->m_allowPathFromDevice = allowPathFromDevice;
    if (d->m_pathChooserDisplay)
        d->m_pathChooserDisplay->setAllowPathFromDevice(allowPathFromDevice);
}

void FilePathAspect::setValidatePlaceHolder(bool validatePlaceHolder)
{
    d->m_validatePlaceHolder = validatePlaceHolder;
    if (d->m_pathChooserDisplay)
        d->m_pathChooserDisplay->lineEdit()->setValidatePlaceHolder(validatePlaceHolder);
}

/*!
    Sets \a elideMode as label elide mode.
*/
void StringAspect::setElideMode(Qt::TextElideMode elideMode)
{
    d->m_elideMode = elideMode;
    if (d->m_labelDisplay)
        d->m_labelDisplay->setElideMode(elideMode);
}

/*!
    Sets \a historyCompleterKey as key for the history completer settings for
    line edits and path chooser displays.

    \sa Utils::PathChooser::setExpectedKind()
*/
void StringAspect::setHistoryCompleter(const QString &historyCompleterKey)
{
    d->m_historyCompleterKey = historyCompleterKey;
    if (d->m_lineEditDisplay)
        d->m_lineEditDisplay->setHistoryCompleter(historyCompleterKey);
    if (d->m_pathChooserDisplay)
        d->m_pathChooserDisplay->setHistoryCompleter(historyCompleterKey);
}

/*!
  Sets \a expectedKind as expected kind for path chooser displays.

  \sa Utils::PathChooser::setExpectedKind()
*/
void StringAspect::setExpectedKind(const PathChooser::Kind expectedKind)
{
    d->m_expectedKind = expectedKind;
    if (d->m_pathChooserDisplay)
        d->m_pathChooserDisplay->setExpectedKind(expectedKind);
}

void StringAspect::setEnvironment(const Environment &env)
{
    d->m_environment = env;
    if (d->m_pathChooserDisplay)
        d->m_pathChooserDisplay->setEnvironment(env);
}

void StringAspect::setBaseFileName(const FilePath &baseFileName)
{
    d->m_baseFileName = baseFileName;
    if (d->m_pathChooserDisplay)
        d->m_pathChooserDisplay->setBaseDirectory(baseFileName);
}

void StringAspect::setUndoRedoEnabled(bool undoRedoEnabled)
{
    d->m_undoRedoEnabled = undoRedoEnabled;
    if (d->m_textEditDisplay)
        d->m_textEditDisplay->setUndoRedoEnabled(undoRedoEnabled);
}

void StringAspect::setAcceptRichText(bool acceptRichText)
{
    d->m_acceptRichText = acceptRichText;
    if (d->m_textEditDisplay)
        d->m_textEditDisplay->setAcceptRichText(acceptRichText);
}

void StringAspect::setMacroExpanderProvider(const MacroExpanderProvider &expanderProvider)
{
    d->m_expanderProvider = expanderProvider;
}

void StringAspect::setUseGlobalMacroExpander()
{
    d->m_expanderProvider = &globalMacroExpander;
}

void StringAspect::setUseResetButton()
{
    d->m_useResetButton = true;
}

void StringAspect::setValidationFunction(const FancyLineEdit::ValidationFunction &validator)
{
    d->m_validator = validator;
    if (d->m_lineEditDisplay)
        d->m_lineEditDisplay->setValidationFunction(*d->m_validator);
    else if (d->m_pathChooserDisplay)
        d->m_pathChooserDisplay->setValidationFunction(*d->m_validator);
}

void FilePathAspect::setOpenTerminalHandler(const std::function<void ()> &openTerminal)
{
    d->m_openTerminal = openTerminal;
    if (d->m_pathChooserDisplay)
        d->m_pathChooserDisplay->setOpenTerminalHandler(openTerminal);
}

void StringAspect::setAutoApplyOnEditingFinished(bool applyOnEditingFinished)
{
    d->m_autoApplyOnEditingFinished = applyOnEditingFinished;
}

void StringAspect::validateInput()
{
    if (d->m_pathChooserDisplay)
        d->m_pathChooserDisplay->triggerChanged();
    if (d->m_lineEditDisplay)
        d->m_lineEditDisplay->validate();
}

void StringAspect::setUncheckedSemantics(StringAspect::UncheckedSemantics semantics)
{
    d->m_uncheckedSemantics = semantics;
}

void StringAspect::addToLayout(LayoutItem &parent)
{
    if (d->m_checker && d->m_checkBoxPlacement == CheckBoxPlacement::Top) {
        d->m_checker->addToLayout(parent);
        parent.addItem(br);
    }

    const auto useMacroExpander = [this](QWidget *w) {
        if (!d->m_expanderProvider)
            return;
        const auto chooser = new VariableChooser(w);
        chooser->addSupportedWidget(w);
        chooser->addMacroExpanderProvider(d->m_expanderProvider);
    };

    const QString displayedString = d->m_displayFilter ? d->m_displayFilter(value()) : value();

    switch (d->m_displayStyle) {
    case PathChooserDisplay:
        d->m_pathChooserDisplay = createSubWidget<PathChooser>();
        d->m_pathChooserDisplay->setExpectedKind(d->m_expectedKind);
        if (!d->m_historyCompleterKey.isEmpty())
            d->m_pathChooserDisplay->setHistoryCompleter(d->m_historyCompleterKey);

        if (d->m_validator)
            d->m_pathChooserDisplay->setValidationFunction(*d->m_validator);
        d->m_pathChooserDisplay->setEnvironment(d->m_environment);
        d->m_pathChooserDisplay->setBaseDirectory(d->m_baseFileName);
        d->m_pathChooserDisplay->setOpenTerminalHandler(d->m_openTerminal);
        d->m_pathChooserDisplay->setPromptDialogFilter(d->m_prompDialogFilter);
        d->m_pathChooserDisplay->setPromptDialogTitle(d->m_prompDialogTitle);
        d->m_pathChooserDisplay->setCommandVersionArguments(d->m_commandVersionArguments);
        d->m_pathChooserDisplay->setAllowPathFromDevice(d->m_allowPathFromDevice);
        d->m_pathChooserDisplay->setReadOnly(isReadOnly());
        d->m_pathChooserDisplay->lineEdit()->setValidatePlaceHolder(d->m_validatePlaceHolder);
        if (defaultValue() == value())
            d->m_pathChooserDisplay->setDefaultValue(defaultValue());
        else
            d->m_pathChooserDisplay->setFilePath(FilePath::fromUserInput(displayedString));
        // do not override default value with placeholder, but use placeholder if default is empty
        if (d->m_pathChooserDisplay->lineEdit()->placeholderText().isEmpty())
            d->m_pathChooserDisplay->lineEdit()->setPlaceholderText(d->m_placeHolderText);
        d->updateWidgetFromCheckStatus(this, d->m_pathChooserDisplay.data());
        addLabeledItem(parent, d->m_pathChooserDisplay);
        useMacroExpander(d->m_pathChooserDisplay->lineEdit());
        connect(d->m_pathChooserDisplay, &PathChooser::validChanged, this, &StringAspect::validChanged);
        bufferToGui();
        if (isAutoApply() && d->m_autoApplyOnEditingFinished) {
            connect(d->m_pathChooserDisplay, &PathChooser::editingFinished,
                    this, &StringAspect::handleGuiChanged);
            connect(d->m_pathChooserDisplay, &PathChooser::browsingFinished,
                    this, &StringAspect::handleGuiChanged);
        } else {
            connect(d->m_pathChooserDisplay, &PathChooser::textChanged,
                    this, &StringAspect::handleGuiChanged);
        }
        break;
    case LineEditDisplay:
        d->m_lineEditDisplay = createSubWidget<FancyLineEdit>();
        d->m_lineEditDisplay->setPlaceholderText(d->m_placeHolderText);
        if (!d->m_historyCompleterKey.isEmpty())
            d->m_lineEditDisplay->setHistoryCompleter(d->m_historyCompleterKey);

        if (d->m_validator)
            d->m_lineEditDisplay->setValidationFunction(*d->m_validator);
        d->m_lineEditDisplay->setTextKeepingActiveCursor(displayedString);
        d->m_lineEditDisplay->setReadOnly(isReadOnly());
        d->m_lineEditDisplay->setValidatePlaceHolder(d->m_validatePlaceHolder);
        d->updateWidgetFromCheckStatus(this, d->m_lineEditDisplay.data());
        addLabeledItem(parent, d->m_lineEditDisplay);
        useMacroExpander(d->m_lineEditDisplay);
        if (d->m_useResetButton) {
            auto resetButton = createSubWidget<QPushButton>(Tr::tr("Reset"));
            resetButton->setEnabled(d->m_lineEditDisplay->text() != defaultValue());
            connect(resetButton, &QPushButton::clicked, this, [this] {
                d->m_lineEditDisplay->setText(defaultValue());
            });
            connect(d->m_lineEditDisplay, &QLineEdit::textChanged, this, [this, resetButton] {
                resetButton->setEnabled(d->m_lineEditDisplay->text() != defaultValue());
            });
            parent.addItem(resetButton);
        }
        connect(d->m_lineEditDisplay, &FancyLineEdit::validChanged, this, &StringAspect::validChanged);
        bufferToGui();
        if (isAutoApply() && d->m_autoApplyOnEditingFinished) {
            connect(d->m_lineEditDisplay, &FancyLineEdit::editingFinished,
                    this, &StringAspect::handleGuiChanged);
        } else {
            connect(d->m_lineEditDisplay, &QLineEdit::textEdited,
                    this, &StringAspect::handleGuiChanged);
        }
        break;
    case TextEditDisplay:
        d->m_textEditDisplay = createSubWidget<QTextEdit>();
        d->m_textEditDisplay->setPlaceholderText(d->m_placeHolderText);
        d->m_textEditDisplay->setUndoRedoEnabled(d->m_undoRedoEnabled);
        d->m_textEditDisplay->setAcceptRichText(d->m_acceptRichText);
        d->m_textEditDisplay->setTextInteractionFlags(Qt::TextEditorInteraction);
        d->m_textEditDisplay->setText(displayedString);
        d->m_textEditDisplay->setReadOnly(isReadOnly());
        d->updateWidgetFromCheckStatus(this, d->m_textEditDisplay.data());
        addLabeledItem(parent, d->m_textEditDisplay);
        useMacroExpander(d->m_textEditDisplay);
        bufferToGui();
        connect(d->m_textEditDisplay, &QTextEdit::textChanged,
                this, &StringAspect::handleGuiChanged);
        break;
    case LabelDisplay:
        d->m_labelDisplay = createSubWidget<ElidingLabel>();
        d->m_labelDisplay->setElideMode(d->m_elideMode);
        d->m_labelDisplay->setTextInteractionFlags(Qt::TextSelectableByMouse);
        d->m_labelDisplay->setText(displayedString);
        d->m_labelDisplay->setToolTip(d->m_showToolTipOnLabel ? displayedString : toolTip());
        addLabeledItem(parent, d->m_labelDisplay);
        break;
    }

    if (d->m_checker && d->m_checkBoxPlacement == CheckBoxPlacement::Right)
        d->m_checker->addToLayout(parent);
}

void StringAspect::guiToBuffer()
{
    switch (d->m_displayStyle) {
    case PathChooserDisplay:
        if (d->m_pathChooserDisplay)
            m_buffer = d->m_pathChooserDisplay->lineEdit()->text();
        break;
    case LineEditDisplay:
        if (d->m_lineEditDisplay)
            m_buffer = d->m_lineEditDisplay->text();
        break;
    case TextEditDisplay:
        if (d->m_textEditDisplay)
            m_buffer = d->m_textEditDisplay->document()->toPlainText();
    case LabelDisplay:
        break;
    }
}

void StringAspect::bufferToInternal()
{
    if (d->m_valueAcceptor) {
        if (const std::optional<QString> tmp = d->m_valueAcceptor(m_internal, m_buffer))
            m_internal = *tmp;
        return;
    }

    m_internal = m_buffer;
}

void StringAspect::internalToBuffer()
{
    m_buffer = d->m_displayFilter ? d->m_displayFilter(m_internal) : m_internal;
}

void StringAspect::bufferToGui()
{
    if (d->m_pathChooserDisplay) {
        d->m_pathChooserDisplay->lineEdit()->setText(m_buffer);
        d->updateWidgetFromCheckStatus(this, d->m_pathChooserDisplay.data());
    }

    if (d->m_lineEditDisplay) {
        d->m_lineEditDisplay->setTextKeepingActiveCursor(m_buffer);
        d->updateWidgetFromCheckStatus(this, d->m_lineEditDisplay.data());
    }

    if (d->m_textEditDisplay) {
        const QString old = d->m_textEditDisplay->document()->toPlainText();
        if (m_buffer != old)
            d->m_textEditDisplay->setText(m_buffer);
        d->updateWidgetFromCheckStatus(this, d->m_textEditDisplay.data());
    }

    if (d->m_labelDisplay) {
        d->m_labelDisplay->setText(m_buffer);
        d->m_labelDisplay->setToolTip(d->m_showToolTipOnLabel ? m_buffer : toolTip());
    }

    validateInput();
}

/*!
    Adds a check box with a \a checkerLabel according to \a checkBoxPlacement
    to the line edit.

    The state of the check box is made persistent when using a non-emtpy
    \a checkerKey.
*/
void StringAspect::makeCheckable(CheckBoxPlacement checkBoxPlacement,
                                     const QString &checkerLabel, const QString &checkerKey)
{
    QTC_ASSERT(!d->m_checker, return);
    d->m_checkBoxPlacement = checkBoxPlacement;
    d->m_checker.reset(new BoolAspect);
    d->m_checker->setLabel(checkerLabel, checkBoxPlacement == CheckBoxPlacement::Top
                           ? BoolAspect::LabelPlacement::InExtraLabel
                           : BoolAspect::LabelPlacement::AtCheckBox);
    d->m_checker->setSettingsKey(checkerKey);

    connect(d->m_checker.get(), &BoolAspect::changed, this, [this] {
        internalToBuffer();
        bufferToGui();
        emit changed();
        checkedChanged();
    });

    connect(d->m_checker.get(), &BoolAspect::volatileValueChanged, this, [this] {
        internalToBuffer();
        bufferToGui();
        checkedChanged();
    });

    internalToBuffer();
    bufferToGui();
}


/*!
    \class Utils::FilePathAspect
    \inmodule QtCreator

    \brief A file path aspect is shallow wrapper around a Utils::StringAspect that
    represents a file in the file system.

    It is displayed by default using Utils::PathChooser.

    The visual representation often contains a label in front of the display
    of the actual value.

    \sa Utils::StringAspect
*/


FilePathAspect::FilePathAspect(AspectContainer *container)
    : StringAspect(container)
{
    setDisplayStyle(PathChooserDisplay);
}

FilePath FilePathAspect::operator()() const
{
    return filePath();
}

FilePath FilePathAspect::value() const
{
    return filePath();
}

QString FilePathAspect::stringValue() const
{
    return StringAspect::value();
}

/*!
    Sets the value of this file path aspect to \a value.

    \note This does not use any check that the value is actually
    a file path.
*/

void FilePathAspect::setValue(const FilePath &filePath)
{
    StringAspect::setValue(filePath.toUserOutput());
}

void FilePathAspect::setDefaultValue(const FilePath &filePath)
{
    StringAspect::setDefaultValue(filePath.toUserOutput());
}

/*!
    \class Utils::ColorAspect
    \inmodule QtCreator

    \brief A color aspect is a color property of some object, together with
    a description of its behavior for common operations like visualizing or
    persisting.

    The color aspect is displayed using a QtColorButton.
*/

ColorAspect::ColorAspect(AspectContainer *container)
    : TypedAspect(container), d(new Internal::ColorAspectPrivate)
{
    setDefaultValue(QColor::fromRgb(0, 0, 0));
    setSpan(1, 1);
}

ColorAspect::~ColorAspect() = default;

void ColorAspect::addToLayout(Layouting::LayoutItem &parent)
{
    QTC_CHECK(!d->m_colorButton);
    d->m_colorButton = createSubWidget<QtColorButton>();
    parent.addItem(d->m_colorButton.data());

    bufferToGui();
    connect(d->m_colorButton.data(), &QtColorButton::colorChanged,
            this, &ColorAspect::handleGuiChanged);
}

void ColorAspect::guiToBuffer()
{
    if (d->m_colorButton)
        m_buffer = d->m_colorButton->color();
}

void ColorAspect::bufferToGui()
{
    if (d->m_colorButton)
        d->m_colorButton->setColor(m_buffer);
}

/*!
    \class Utils::BoolAspect
    \inmodule QtCreator

    \brief A boolean aspect is a boolean property of some object, together with
    a description of its behavior for common operations like visualizing or
    persisting.

    The boolean aspect is displayed using a QCheckBox.

    The visual representation often contains a label in front or after
    the display of the actual checkmark.
*/


BoolAspect::BoolAspect(AspectContainer *container)
    : TypedAspect(container), d(new Internal::BoolAspectPrivate)
{
    setDefaultValue(false);
    setSpan(2, 1);
}

/*!
    \internal
*/
BoolAspect::~BoolAspect() = default;

/*!
    \reimp
*/
void BoolAspect::addToLayout(Layouting::LayoutItem &parent)
{
    QTC_ASSERT(m_buffer == m_internal, m_buffer = m_internal);

    if (!d->m_buttonIsAdopted) {
        QTC_CHECK(!d->m_button);
        d->m_button = createSubWidget<QCheckBox>();
    }
    switch (d->m_labelPlacement) {
    case LabelPlacement::AtCheckBoxWithoutDummyLabel:
        d->m_button->setText(labelText());
        parent.addItem(d->m_button.data());
        break;
    case LabelPlacement::AtCheckBox:
        d->m_button->setText(labelText());
        parent.addItem(empty());
        parent.addItem(d->m_button.data());
        break;
    case LabelPlacement::InExtraLabel:
        addLabeledItem(parent, d->m_button);
        break;
    }

    bufferToGui();
    connect(d->m_button.data(), &QAbstractButton::clicked,
            this, &BoolAspect::handleGuiChanged);
}

void BoolAspect::adoptButton(QAbstractButton *button)
{
    QTC_ASSERT(button, return);
    QTC_CHECK(!d->m_button);
    d->m_button = button;
    d->m_buttonIsAdopted = true;
    registerSubWidget(button);
}

std::function<void (QObject *)> BoolAspect::groupChecker()
{
    return [this](QObject *target) {
        auto groupBox = qobject_cast<QGroupBox *>(target);
        QTC_ASSERT(groupBox, return);
        registerSubWidget(groupBox);
        groupBox->setCheckable(true);
        groupBox->setChecked(value());
        d->m_groupBox = groupBox;
    };
}

QAction *BoolAspect::action()
{
    if (hasAction())
        return TypedAspect::action();
    auto act = TypedAspect::action(); // Creates it.
    act->setCheckable(true);
    act->setChecked(m_internal);
    act->setToolTip(toolTip());
    connect(act, &QAction::triggered, this, [this](bool newValue) {
        setValue(newValue);
    });
    return act;
}

void BoolAspect::guiToBuffer()
{
    if (d->m_button)
        m_buffer = d->m_button->isChecked();
    else if (d->m_groupBox)
        m_buffer = d->m_groupBox->isChecked();
}

void BoolAspect::bufferToGui()
{
    if (d->m_button)
        d->m_button->setChecked(m_buffer);
    else if (d->m_groupBox)
        d->m_groupBox->setChecked(m_buffer);
}

void BoolAspect::setLabel(const QString &labelText, LabelPlacement labelPlacement)
{
    TypedAspect::setLabelText(labelText);
    d->m_labelPlacement = labelPlacement;
}

void BoolAspect::setLabelPlacement(BoolAspect::LabelPlacement labelPlacement)
{
    d->m_labelPlacement = labelPlacement;
}

CheckableDecider BoolAspect::askAgainCheckableDecider()
{
    return CheckableDecider(
        [this] { return value(); },
        [this] { setValue(false); }
    );
}

CheckableDecider BoolAspect::doNotAskAgainCheckableDecider()
{
    return CheckableDecider(
        [this] { return !value(); },
        [this] { setValue(true); }
    );
}

/*!
    \class Utils::SelectionAspect
    \inmodule QtCreator

    \brief A selection aspect represents a specific choice out of
    several.

    The selection aspect is displayed using a QComboBox or
    QRadioButtons in a QButtonGroup.
*/

SelectionAspect::SelectionAspect(AspectContainer *container)
    : TypedAspect(container), d(new Internal::SelectionAspectPrivate)
{
    setSpan(2, 1);
}

/*!
    \internal
*/
SelectionAspect::~SelectionAspect() = default;

/*!
    \reimp
*/
void SelectionAspect::addToLayout(Layouting::LayoutItem &parent)
{
    QTC_CHECK(d->m_buttonGroup == nullptr);
    QTC_CHECK(!d->m_comboBox);
    QTC_ASSERT(d->m_buttons.isEmpty(), d->m_buttons.clear());
    QTC_ASSERT(m_buffer == m_internal, m_buffer = m_internal);

    switch (d->m_displayStyle) {
    case DisplayStyle::RadioButtons:
        d->m_buttonGroup = new QButtonGroup();
        d->m_buttonGroup->setExclusive(true);
        for (int i = 0, n = d->m_options.size(); i < n; ++i) {
            const Option &option = d->m_options.at(i);
            auto button = createSubWidget<QRadioButton>(option.displayName);
            button->setChecked(i == value());
            button->setEnabled(option.enabled);
            button->setToolTip(option.tooltip);
            parent.addItem(button);
            d->m_buttons.append(button);
            d->m_buttonGroup->addButton(button, i);
        }
        bufferToGui();
        connect(d->m_buttonGroup, &QButtonGroup::idClicked,
                this, &SelectionAspect::handleGuiChanged);
        break;
    case DisplayStyle::ComboBox:
        setLabelText(displayName());
        d->m_comboBox = createSubWidget<QComboBox>();
        for (int i = 0, n = d->m_options.size(); i < n; ++i)
            d->m_comboBox->addItem(d->m_options.at(i).displayName);
        d->m_comboBox->setCurrentIndex(value());
        addLabeledItem(parent, d->m_comboBox);
        bufferToGui();
        connect(d->m_comboBox.data(), &QComboBox::activated,
                this, &SelectionAspect::handleGuiChanged);
        break;
    }
}

void SelectionAspect::guiToBuffer()
{
    switch (d->m_displayStyle) {
    case DisplayStyle::RadioButtons:
        if (d->m_buttonGroup)
            m_buffer = d->m_buttonGroup->checkedId();
        break;
    case DisplayStyle::ComboBox:
        if (d->m_comboBox)
            m_buffer = d->m_comboBox->currentIndex();
        break;
    }
}

void SelectionAspect::bufferToGui()
{
    switch (d->m_displayStyle) {
    case DisplayStyle::RadioButtons: {
        if (d->m_buttonGroup) {
            QAbstractButton *button = d->m_buttonGroup->button(m_buffer);
            QTC_ASSERT(button, return);
            button->setChecked(true);
        }
        break;
    }
    case DisplayStyle::ComboBox:
        if (d->m_comboBox)
            d->m_comboBox->setCurrentIndex(m_buffer);
        break;
    }
}

void SelectionAspect::finish()
{
    delete d->m_buttonGroup;
    d->m_buttonGroup = nullptr;
    BaseAspect::finish();
    d->m_buttons.clear();
}

void SelectionAspect::setDisplayStyle(SelectionAspect::DisplayStyle style)
{
    d->m_displayStyle = style;
}

void SelectionAspect::setStringValue(const QString &val)
{
    const int index = indexForDisplay(val);
    QTC_ASSERT(index >= 0, return);
    setValue(index);
}

void SelectionAspect::setDefaultValue(int val)
{
    TypedAspect::setDefaultValue(val);
}

// Note: This needs to be set after all options are added.
void SelectionAspect::setDefaultValue(const QString &val)
{
    TypedAspect::setDefaultValue(indexForDisplay(val));
}

QString SelectionAspect::stringValue() const
{
    const int idx = value();
    return idx >= 0 && idx < d->m_options.size() ? d->m_options.at(idx).displayName : QString();
}

QVariant SelectionAspect::itemValue() const
{
    const int idx = value();
    return idx >= 0 && idx < d->m_options.size() ? d->m_options.at(idx).itemData : QVariant();
}

void SelectionAspect::addOption(const QString &displayName, const QString &toolTip)
{
    d->m_options.append(Option(displayName, toolTip, {}));
}

void SelectionAspect::addOption(const Option &option)
{
    d->m_options.append(option);
}

int SelectionAspect::indexForDisplay(const QString &displayName) const
{
    for (int i = 0, n = d->m_options.size(); i < n; ++i) {
        if (d->m_options.at(i).displayName == displayName)
            return i;
    }
    return -1;
}

QString SelectionAspect::displayForIndex(int index) const
{
    QTC_ASSERT(index >= 0 && index < d->m_options.size(), return {});
    return d->m_options.at(index).displayName;
}

int SelectionAspect::indexForItemValue(const QVariant &value) const
{
    for (int i = 0, n = d->m_options.size(); i < n; ++i) {
        if (d->m_options.at(i).itemData == value)
            return i;
    }
    return -1;
}

QVariant SelectionAspect::itemValueForIndex(int index) const
{
    QTC_ASSERT(index >= 0 && index < d->m_options.size(), return {});
    return d->m_options.at(index).itemData;
}

/*!
    \class Utils::MultiSelectionAspect
    \inmodule QtCreator

    \brief A multi-selection aspect represents one or more choices out of
    several.

    The multi-selection aspect is displayed using a QListWidget with
    checkable items.
*/

MultiSelectionAspect::MultiSelectionAspect(AspectContainer *container)
    : TypedAspect(container), d(new Internal::MultiSelectionAspectPrivate(this))
{
    setDefaultValue(QStringList());
    setSpan(2, 1);
}

/*!
    \internal
*/
MultiSelectionAspect::~MultiSelectionAspect() = default;

/*!
    \reimp
*/
void MultiSelectionAspect::addToLayout(LayoutItem &builder)
{
    QTC_CHECK(d->m_listView == nullptr);
    if (d->m_allValues.isEmpty())
        return;

    switch (d->m_displayStyle) {
    case DisplayStyle::ListView:
        d->m_listView = createSubWidget<QListWidget>();
        for (const QString &val : std::as_const(d->m_allValues))
            (void) new QListWidgetItem(val, d->m_listView);
        addLabeledItem(builder, d->m_listView);

        bufferToGui();
        connect(d->m_listView, &QListWidget::itemChanged,
                this, &MultiSelectionAspect::handleGuiChanged);
    }
}

bool Internal::MultiSelectionAspectPrivate::setValueSelectedHelper(const QString &val, bool on)
{
    QStringList list = q->value();
    if (on && !list.contains(val)) {
        list.append(val);
        q->setValue(list);
        return true;
    }
    if (!on && list.contains(val)) {
        list.removeOne(val);
        q->setValue(list);
        return true;
    }
    return false;
}

QStringList MultiSelectionAspect::allValues() const
{
    return d->m_allValues;
}

void MultiSelectionAspect::setAllValues(const QStringList &val)
{
    d->m_allValues = val;
}

void MultiSelectionAspect::setDisplayStyle(MultiSelectionAspect::DisplayStyle style)
{
    d->m_displayStyle = style;
}

void MultiSelectionAspect::bufferToGui()
{
    if (d->m_listView) {
        const int n = d->m_listView->count();
        QTC_CHECK(n == d->m_allValues.size());
        for (int i = 0; i != n; ++i) {
            auto item = d->m_listView->item(i);
            item->setCheckState(m_buffer.contains(item->text()) ? Qt::Checked : Qt::Unchecked);
        }
    }
}

void MultiSelectionAspect::guiToBuffer()
{
    if (d->m_listView) {
        m_buffer.clear();
        const int n = d->m_listView->count();
        QTC_CHECK(n == d->m_allValues.size());
        for (int i = 0; i != n; ++i) {
            auto item = d->m_listView->item(i);
            if (item->checkState() == Qt::Checked)
                m_buffer.append(item->text());
        }
    }
}


/*!
    \class Utils::IntegerAspect
    \inmodule QtCreator

    \brief An integer aspect is a integral property of some object, together with
    a description of its behavior for common operations like visualizing or
    persisting.

    The integer aspect is displayed using a \c QSpinBox.

    The visual representation often contains a label in front
    the display of the spin box.
*/

// IntegerAspect

IntegerAspect::IntegerAspect(AspectContainer *container)
    : TypedAspect(container), d(new Internal::IntegerAspectPrivate)
{
    setSpan(2, 1);
}

/*!
    \internal
*/
IntegerAspect::~IntegerAspect() = default;

/*!
    \reimp
*/
void IntegerAspect::addToLayout(Layouting::LayoutItem &parent)
{
    QTC_CHECK(!d->m_spinBox);
    d->m_spinBox = createSubWidget<QSpinBox>();
    d->m_spinBox->setDisplayIntegerBase(d->m_displayIntegerBase);
    d->m_spinBox->setPrefix(d->m_prefix);
    d->m_spinBox->setSuffix(d->m_suffix);
    d->m_spinBox->setSingleStep(d->m_singleStep);
    d->m_spinBox->setSpecialValueText(d->m_specialValueText);
    if (d->m_maximumValue && d->m_maximumValue)
        d->m_spinBox->setRange(int(d->m_minimumValue.value() / d->m_displayScaleFactor),
                               int(d->m_maximumValue.value() / d->m_displayScaleFactor));
    d->m_spinBox->setValue(int(value() / d->m_displayScaleFactor)); // Must happen after setRange()
    addLabeledItem(parent, d->m_spinBox);

    if (isAutoApply()) {
        connect(d->m_spinBox.data(), &QSpinBox::valueChanged,
                this, [this] { setValue(d->m_spinBox->value() * d->m_displayScaleFactor); });
    }
}

void IntegerAspect::guiToBuffer()
{
    if (d->m_spinBox)
        m_buffer = d->m_spinBox->value() * d->m_displayScaleFactor;
}

void IntegerAspect::bufferToGui()
{
    if (d->m_spinBox)
        d->m_spinBox->setValue(m_buffer / d->m_displayScaleFactor);
}

void IntegerAspect::setRange(qint64 min, qint64 max)
{
    d->m_minimumValue = min;
    d->m_maximumValue = max;
}

void IntegerAspect::setLabel(const QString &label)
{
    setLabelText(label);
}

void IntegerAspect::setPrefix(const QString &prefix)
{
    d->m_prefix = prefix;
}

void IntegerAspect::setSuffix(const QString &suffix)
{
    d->m_suffix = suffix;
}

void IntegerAspect::setDisplayIntegerBase(int base)
{
    d->m_displayIntegerBase = base;
}

void IntegerAspect::setDisplayScaleFactor(qint64 factor)
{
    d->m_displayScaleFactor = factor;
}

void IntegerAspect::setSpecialValueText(const QString &specialText)
{
    d->m_specialValueText = specialText;
}

void IntegerAspect::setSingleStep(qint64 step)
{
    d->m_singleStep = step;
}


/*!
    \class Utils::DoubleAspect
    \inmodule QtCreator

    \brief An double aspect is a numerical property of some object, together with
    a description of its behavior for common operations like visualizing or
    persisting.

    The double aspect is displayed using a \c QDoubleSpinBox.

    The visual representation often contains a label in front
    the display of the spin box.
*/

DoubleAspect::DoubleAspect(AspectContainer *container)
    : TypedAspect(container), d(new Internal::DoubleAspectPrivate)
{
    setDefaultValue(double(0));
    setSpan(2, 1);
}

/*!
    \internal
*/
DoubleAspect::~DoubleAspect() = default;

/*!
    \reimp
*/
void DoubleAspect::addToLayout(LayoutItem &builder)
{
    QTC_CHECK(!d->m_spinBox);
    d->m_spinBox = createSubWidget<QDoubleSpinBox>();
    d->m_spinBox->setPrefix(d->m_prefix);
    d->m_spinBox->setSuffix(d->m_suffix);
    d->m_spinBox->setSingleStep(d->m_singleStep);
    d->m_spinBox->setSpecialValueText(d->m_specialValueText);
    if (d->m_maximumValue && d->m_maximumValue)
        d->m_spinBox->setRange(d->m_minimumValue.value(), d->m_maximumValue.value());
    bufferToGui(); // Must happen after setRange()!
    addLabeledItem(builder, d->m_spinBox);

    if (isAutoApply()) {
        connect(d->m_spinBox.data(), &QDoubleSpinBox::valueChanged,
                this, [this] { setValue(d->m_spinBox->value()); });
    }
}

void DoubleAspect::guiToBuffer()
{
    if (d->m_spinBox)
        m_buffer = d->m_spinBox->value();
}

void DoubleAspect::bufferToGui()
{
    if (d->m_spinBox)
        d->m_spinBox->setValue(m_buffer);
}

void DoubleAspect::setRange(double min, double max)
{
    d->m_minimumValue = min;
    d->m_maximumValue = max;
}

void DoubleAspect::setPrefix(const QString &prefix)
{
    d->m_prefix = prefix;
}

void DoubleAspect::setSuffix(const QString &suffix)
{
    d->m_suffix = suffix;
}

void DoubleAspect::setSpecialValueText(const QString &specialText)
{
    d->m_specialValueText = specialText;
}

void DoubleAspect::setSingleStep(double step)
{
    d->m_singleStep = step;
}


/*!
    \class Utils::TriStateAspect
    \inmodule QtCreator

    \brief A tristate aspect is a property of some object that can have
    three values: enabled, disabled, and unspecified.

    Its visual representation is a QComboBox with three items.
*/

TriStateAspect::TriStateAspect(AspectContainer *container,
                               const QString &onString,
                               const QString &offString,
                               const QString &defaultString)
    : SelectionAspect(container)
{
    setDisplayStyle(DisplayStyle::ComboBox);
    setDefaultValue(TriState::Default);
    addOption(onString.isEmpty() ? Tr::tr("Enable") : onString);
    addOption(offString.isEmpty() ? Tr::tr("Disable") : offString);
    addOption(defaultString.isEmpty() ? Tr::tr("Leave at Default") : defaultString);
}

TriState TriStateAspect::value() const
{
    return TriState::fromInt(SelectionAspect::value());
}

void TriStateAspect::setValue(TriState value)
{
    SelectionAspect::setValue(value.toInt());
}

TriState TriStateAspect::defaultValue() const
{
    return TriState::fromInt(SelectionAspect::defaultValue());
}

void TriStateAspect::setDefaultValue(TriState value)
{
    SelectionAspect::setDefaultValue(value.toInt());
}

const TriState TriState::Enabled{TriState::EnabledValue};
const TriState TriState::Disabled{TriState::DisabledValue};
const TriState TriState::Default{TriState::DefaultValue};

TriState TriState::fromVariant(const QVariant &variant)
{
    return fromInt(variant.toInt());
}

TriState TriState::fromInt(int v)
{
    QTC_ASSERT(v == EnabledValue || v == DisabledValue || v == DefaultValue, v = DefaultValue);
    return TriState(Value(v));
}


/*!
    \class Utils::StringListAspect
    \inmodule QtCreator

    \brief A string list aspect represents a property of some object
    that is a list of strings.
*/

StringListAspect::StringListAspect(AspectContainer *container)
    : TypedAspect(container), d(new Internal::StringListAspectPrivate)
{
    setDefaultValue(QStringList());
}

/*!
    \internal
*/
StringListAspect::~StringListAspect() = default;

/*!
    \reimp
*/
void StringListAspect::addToLayout(LayoutItem &parent)
{
    Q_UNUSED(parent)
    // TODO - when needed.
}

void StringListAspect::appendValue(const QString &s, bool allowDuplicates)
{
    QStringList val = value();
    if (allowDuplicates || !val.contains(s))
        val.append(s);
    setValue(val);
}

void StringListAspect::removeValue(const QString &s)
{
    QStringList val = value();
    val.removeAll(s);
    setValue(val);
}

void StringListAspect::appendValues(const QStringList &values, bool allowDuplicates)
{
    QStringList val = value();
    for (const QString &s : values) {
        if (allowDuplicates || !val.contains(s))
            val.append(s);
    }
    setValue(val);
}

void StringListAspect::removeValues(const QStringList &values)
{
    QStringList val = value();
    for (const QString &s : values)
        val.removeAll(s);
    setValue(val);
}

/*!
    \class Utils::IntegerListAspect
    \internal
    \inmodule QtCreator

    \brief A string list aspect represents a property of some object
    that is a list of strings.
*/

IntegersAspect::IntegersAspect(AspectContainer *container)
    : TypedAspect(container)
{}

/*!
    \internal
*/
IntegersAspect::~IntegersAspect() = default;

/*!
    \reimp
*/
void IntegersAspect::addToLayout(Layouting::LayoutItem &parent)
{
    Q_UNUSED(parent)
    // TODO - when needed.
}


/*!
    \class Utils::TextDisplay
    \inmodule QtCreator

    \brief A text display is a phony aspect with the sole purpose of providing
    some text display using an Utils::InfoLabel in places where otherwise
    more expensive Utils::StringAspect items would be used.

    A text display does not have a real value.
*/

/*!
    Constructs a text display showing the \a message with an icon representing
    type \a type.
 */
TextDisplay::TextDisplay(AspectContainer *container, const QString &message, InfoLabel::InfoType type)
    : BaseAspect(container), d(new Internal::TextDisplayPrivate)
{
    d->m_message = message;
    d->m_type = type;
}

/*!
    \internal
*/
TextDisplay::~TextDisplay() = default;

/*!
    \reimp
*/
void TextDisplay::addToLayout(LayoutItem &parent)
{
    if (!d->m_label) {
        d->m_label = createSubWidget<InfoLabel>(d->m_message, d->m_type);
        d->m_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        d->m_label->setElideMode(Qt::ElideNone);
        d->m_label->setWordWrap(true);
        // Do not use m_label->setVisible(isVisible()) unconditionally, it does not
        // have a QWidget parent yet when used in a LayoutBuilder.
        if (!isVisible())
            d->m_label->setVisible(false);
    }
    parent.addItem(d->m_label.data());
}

/*!
    Sets \a t as the information label type for the visual representation
    of this aspect.
 */
void TextDisplay::setIconType(InfoLabel::InfoType t)
{
    d->m_type = t;
    if (d->m_label)
        d->m_label->setType(t);
}

void TextDisplay::setText(const QString &message)
{
    d->m_message = message;
}

/*!
    \class Utils::AspectContainer
    \inmodule QtCreator

    \brief The AspectContainer class wraps one or more aspects while providing
    the interface of a single aspect.

    Sub-aspects ownership can be declared using \a setOwnsSubAspects.
*/

namespace Internal {

class AspectContainerPrivate
{
public:
    QList<BaseAspect *> m_items; // Both owned and non-owned.
    QList<BaseAspect *> m_ownedItems; // Owned only.
    bool m_autoApply = true;
    QStringList m_settingsGroup;
    std::function<Layouting::LayoutItem ()> m_layouter;
};

} // Internal

AspectContainer::AspectContainer(QObject *parent)
    : QObject(parent), d(new Internal::AspectContainerPrivate)
{}

/*!
    \internal
*/
AspectContainer::~AspectContainer()
{
    qDeleteAll(d->m_ownedItems);
}

/*!
    \internal
*/
void AspectContainer::registerAspect(BaseAspect *aspect, bool takeOwnership)
{
    aspect->setAutoApply(d->m_autoApply);
    d->m_items.append(aspect);
    if (takeOwnership)
        d->m_ownedItems.append(aspect);
}

void AspectContainer::registerAspects(const AspectContainer &aspects)
{
    for (BaseAspect *aspect : std::as_const(aspects.d->m_items))
        registerAspect(aspect);
}

/*!
    Retrieves a BaseAspect with a given \a id, or nullptr if no such aspect is contained.

    \sa BaseAspect
*/
BaseAspect *AspectContainer::aspect(Id id) const
{
    return findOrDefault(d->m_items, equal(&BaseAspect::id, id));
}

AspectContainer::const_iterator AspectContainer::begin() const
{
    return d->m_items.cbegin();
}

AspectContainer::const_iterator AspectContainer::end() const
{
    return d->m_items.cend();
}

void AspectContainer::setLayouter(const std::function<Layouting::LayoutItem ()> &layouter)
{
    d->m_layouter = layouter;
}

std::function<LayoutItem ()> AspectContainer::layouter() const
{
    return d->m_layouter;
}

const QList<BaseAspect *> &AspectContainer::aspects() const
{
    return d->m_items;
}

void AspectContainer::fromMap(const QVariantMap &map)
{
    for (BaseAspect *aspect : std::as_const(d->m_items))
        aspect->fromMap(map);

    emit fromMapFinished();

}

void AspectContainer::toMap(QVariantMap &map) const
{
    for (BaseAspect *aspect : std::as_const(d->m_items))
        aspect->toMap(map);
}

void AspectContainer::readSettings()
{
    QTC_ASSERT(theSettings, return);
    for (const QString &group : d->m_settingsGroup)
        theSettings->beginGroup(group);

    for (BaseAspect *aspect : std::as_const(d->m_items))
        aspect->readSettings();

    for (int i = 0; i != d->m_settingsGroup.size(); ++i)
        theSettings->endGroup();
}

void AspectContainer::writeSettings() const
{
    QTC_ASSERT(theSettings, return);
    for (const QString &group : d->m_settingsGroup)
        theSettings->beginGroup(group);

    for (BaseAspect *aspect : std::as_const(d->m_items))
        aspect->writeSettings();

    for (int i = 0; i != d->m_settingsGroup.size(); ++i)
        theSettings->endGroup();
}

void AspectContainer::setSettingsGroup(const QString &groupKey)
{
    d->m_settingsGroup = QStringList{groupKey};
}

void AspectContainer::setSettingsGroups(const QString &groupKey, const QString &subGroupKey)
{
    d->m_settingsGroup = QStringList{groupKey, subGroupKey};
}

void AspectContainer::apply()
{
    const bool willChange = isDirty();

    for (BaseAspect *aspect : std::as_const(d->m_items))
        aspect->apply();

    emit applied();

    if (willChange)
        emit changed();
}

void AspectContainer::cancel()
{
    for (BaseAspect *aspect : std::as_const(d->m_items))
        aspect->cancel();
}

void AspectContainer::finish()
{
    for (BaseAspect *aspect : std::as_const(d->m_items))
        aspect->finish();
}

void AspectContainer::reset()
{
    for (BaseAspect *aspect : std::as_const(d->m_items))
        aspect->setVariantValueQuietly(aspect->defaultVariantValue());
}

void AspectContainer::setAutoApply(bool on)
{
    d->m_autoApply = on;
    for (BaseAspect *aspect : std::as_const(d->m_items))
        aspect->setAutoApply(on);
}

bool AspectContainer::isDirty() const
{
    for (BaseAspect *aspect : std::as_const(d->m_items)) {
        if (aspect->isDirty())
            return true;
    }
    return false;
}

bool AspectContainer::equals(const AspectContainer &other) const
{
    // FIXME: Expensive, but should not really be needed in a fully aspectified world.
    QVariantMap thisMap, thatMap;
    toMap(thisMap);
    other.toMap(thatMap);
    return thisMap == thatMap;
}

void AspectContainer::copyFrom(const AspectContainer &other)
{
    QVariantMap map;
    other.toMap(map);
    fromMap(map);
}

void AspectContainer::forEachAspect(const std::function<void(BaseAspect *)> &run) const
{
    for (BaseAspect *aspect : std::as_const(d->m_items)) {
        if (auto container = dynamic_cast<AspectContainer *>(aspect))
            container->forEachAspect(run);
        else
            run(aspect);
    }
}

BaseAspect::Data::Ptr BaseAspect::extractData() const
{
    QTC_ASSERT(d->m_dataCreator, return {});
    Data *data = d->m_dataCreator();
    data->m_classId = metaObject();
    data->m_id = id();
    data->m_cloner = d->m_dataCloner;
    for (const DataExtractor &extractor : d->m_dataExtractors)
        extractor(data);
    return Data::Ptr(data);
}

/*
    Mirrors the internal volatile value to the GUI element if they are already
    created.

    No-op otherwise.
*/
void BaseAspect::bufferToGui()
{}

/*
    Mirrors the data stored in GUI element if they are already created to
    the internal volatile value.

    No-op otherwise.
*/
void BaseAspect::guiToBuffer()
{}

/*
    Mirrors buffered volatile value to the internal value.
    This function is used for \c apply().
*/

void BaseAspect::bufferToInternal()
{}


void BaseAspect::internalToBuffer()
{}

/*
    Applies common postprocessing like macro expansion.
*/

void BaseAspect::internalToExternal()
{}

/*
    Mirrors externally visible value to internal volatile value.
*/
void BaseAspect::externalToInternal()
{}

void BaseAspect::handleGuiChanged()
{
    guiToBuffer();
    emit volatileValueChanged();
    if (isAutoApply())
        apply();
}

void BaseAspect::addDataExtractorHelper(const DataExtractor &extractor) const
{
    d->m_dataExtractors.append(extractor);
}

void BaseAspect::setDataCreatorHelper(const DataCreator &creator) const
{
    d->m_dataCreator = creator;
}

void BaseAspect::setDataClonerHelper(const DataCloner &cloner) const
{
    d->m_dataCloner = cloner;
}

const BaseAspect::Data *AspectContainerData::aspect(Id instanceId) const
{
    for (const BaseAspect::Data::Ptr &data : m_data) {
        if (data.get()->id() == instanceId)
            return data.get();
    }
    return nullptr;
}

const BaseAspect::Data *AspectContainerData::aspect(BaseAspect::Data::ClassId classId) const
{
    for (const BaseAspect::Data::Ptr &data : m_data) {
        if (data.get()->classId() == classId)
            return data.get();
    }
    return nullptr;
}

void AspectContainerData::append(const BaseAspect::Data::Ptr &data)
{
    m_data.append(data);
}

// BaseAspect::Data

BaseAspect::Data::~Data() = default;

void BaseAspect::Data::Ptr::operator=(const Ptr &other)
{
    if (this == &other)
        return;
    delete m_data;
    m_data = other.m_data->clone();
}

} // namespace Utils

#ifndef ENTRYWIDGETS_H
#define ENTRYWIDGETS_H

#include <QWidget>
#include <QString>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>

#include "../lib/macros.h"
#include "../lib/frequency.h"
#include "../lib/amplitude.h"
#include "../lib/time_type.h"

#include <QLineEdit>

class FixedSpacer : public QWidget {
public:
    FixedSpacer(QSize size, QWidget *parent = 0) : QWidget(parent) {
        setFixedSize(size);
    }
};

enum EntryType {
    FREQ_ENTRY = 0,
    AMP_ENTRY,
    TIME_ENTRY,
    VALUE_ENTRY
};

class LineEntry : public QLineEdit {
    Q_OBJECT

public:
    LineEntry(EntryType type, QWidget *parent = 0);
    ~LineEntry();

    bool SetFrequency(Frequency freq);
    bool SetAmplitude(Amplitude amp);
    bool SetTime(Time t);
    bool SetValue(double val);
    bool SetValue(double val, QString &units);

    Frequency GetFrequency() const { return frequency; }
    Amplitude GetAmplitude() const { return amplitude; }
    Time GetTime() const { return time; }
    double GetValue() const { return value; }

protected:
    void mouseReleaseEvent(QMouseEvent *) {
        if(selectedText().isEmpty()) selectAll(); }

private:
    EntryType entry_type;

    Frequency frequency;
    Amplitude amplitude;
    Time time;
    double value;

public slots:
    void editChanged();

signals:
    void entryUpdated();
};

/*
 * Custom label for style_sheet convenience
 */
class Label : public QLabel {
    Q_OBJECT

public:
    Label(QWidget *parent = 0, Qt::WindowFlags f = 0)
        : QLabel(parent, f)
    {
        setObjectName("SH_Label");
    }
    Label(const QString &text, QWidget *parent = 0, Qt::WindowFlags f = 0)
        : QLabel(text, parent, f)
    {
        setObjectName("SH_Label");
    }
};

/*
 * Custom combo_box for style_sheet convenience
 */
class ComboBox : public QComboBox {
    Q_OBJECT

public:
    ComboBox(QWidget *parent = 0)
        : QComboBox(parent)
    {
        setObjectName("SH_ComboBox");
        setCurrentIndex(0);
    }
};

/*
 * Simple frequency entry
 * [label - lineedit]
 */
class FrequencyEntry : public QWidget {
    Q_OBJECT

public:
    FrequencyEntry(const QString &label_text,
                   Frequency f,
                   QWidget *parent = 0);
    ~FrequencyEntry();

    void SetFrequency(Frequency &f);
    Frequency GetFrequency() const { return freq; }

private:
    Frequency freq; // local copy
    Label *label;
    LineEntry *entry;

public slots:
    void editUpdated();

signals:
    void freqViewChanged(Frequency);

private:
    DISALLOW_COPY_AND_ASSIGN(FrequencyEntry)
};

/*
 * Shift frequency entry
 * [ label - up_btn - down_btn - line_entry ]
 */
class FreqShiftEntry : public QWidget {
    Q_OBJECT

public:
    FreqShiftEntry(const QString &label_text,
                   Frequency f,
                   QWidget *parent = 0);
    ~FreqShiftEntry();

    void SetFrequency(Frequency f);
    Frequency GetFrequency() const { return freq; }

private:
    Frequency freq; // local copy
    Label *label;
    QPushButton *up_btn, *down_btn;
    LineEntry *entry;

public slots:
    void editUpdated();
    void clickedUp() { emit shift(true); }
    void clickedDown() { emit shift(false); }

signals:
    void freqViewChanged(Frequency f);
    void shift(bool inc); // true for up, false for down
};

/*
 * Combo entry widget
 */
class ComboEntry : public QWidget {
    Q_OBJECT

public:
    ComboEntry(const QString &label_text, QWidget *parent = 0);
    ~ComboEntry();

private:
    Label *label;
    ComboBox *combo_box;

signals:
    void comboIndexChanged(int);

public slots:
    void setComboIndex(int ix);
    void setComboText(const QStringList &list);

private:
    DISALLOW_COPY_AND_ASSIGN(ComboEntry)
};

/*
 * Panel Widget for amplitude entry
 * [ label - value_entry - units_combo_box ]
 */
class AmplitudeEntry : public QWidget {
    Q_OBJECT

public:
    AmplitudeEntry(const QString &label_text,
                   Amplitude a,
                   QWidget *parent = 0);
    ~AmplitudeEntry();

    Amplitude GetAmplitude() const { return amplitude; }
    void SetAmplitude(Amplitude a);

private:
    Amplitude amplitude;
    Label *label;
    QPushButton *up_btn, *down_btn;
    LineEntry *entry;
    ComboBox *units;
    int last_unit_index;

public slots:
    void editUpdated();
    void unitsUpdated(int);
    void clickedUp() { emit shift(true); }
    void clickedDown() { emit shift(false); }

signals:
    void amplitudeChanged(Amplitude);
    void shift(bool);

private:
    DISALLOW_COPY_AND_ASSIGN(AmplitudeEntry)
};

/* Panel Widget for time entry
 * [ label - value_entry - unit_str ]
 */
class TimeEntry : public QWidget {
    Q_OBJECT

public:
    TimeEntry(const QString &label_text, Time t,
              TimeUnit tu, QWidget *parent = 0);
    ~TimeEntry();

    Time GetTime() const { return time; }
    void SetTime(Time t);

private:
    Time time;
    TimeUnit units;

    Label *label;
    LineEntry *entry;
    Label *units_label;

public slots:
    void entryChanged();

signals:
    void timeChanged(Time);

private:
    DISALLOW_COPY_AND_ASSIGN(TimeEntry)
};

/*
 * Generic numeric entry widget
 * Optional units label
 * [ title_label - numeric_entry - (optional)unit_str ]
 */
class NumericEntry : public QWidget {
    Q_OBJECT

public:
    NumericEntry(const QString &label_text, double starting_value,
                 const QString &units_text, QWidget *parent = 0);
    ~NumericEntry();

    double GetValue() const { return value; }
    void SetValue(double v) { value = v; entry->SetValue(v); }

protected:
    void resizeEvent(QResizeEvent *);

private:
    double value;
    Label *label;
    LineEntry *entry;
    Label *units_label;

public slots:
    void entryChanged();

signals:
    void valueChanged(double v);

private:
    DISALLOW_COPY_AND_ASSIGN(NumericEntry)
};

/*
 * Color button widget, standalone button, not
 *  part of a larger entry line widget
 */
class ColorButton : public QPushButton {
    Q_OBJECT

public:
    ColorButton(QWidget *parent = 0);
    ~ColorButton() {}

    void SetColor(QColor c);
    QColor GetColor() const { return color; }

private:
    QColor color;

signals:
    void colorChanged(QColor &);

public slots:
    void onClick(bool);

private:
    DISALLOW_COPY_AND_ASSIGN(ColorButton)
};

/*
 * Line Entry Widget for Color Button
 */
class ColorEntry : public QWidget {
    Q_OBJECT

public:
    ColorEntry(const QString &label_text, QWidget *parent = 0);
    ~ColorEntry();

    void SetColor(QColor c) { color_button->SetColor(c); }
    QColor GetColor() const { return color_button->GetColor(); }

protected:
    void resizeEvent(QResizeEvent *);

private:
    Label *label;
    ColorButton *color_button;

signals:
    void colorChanged(QColor &);

private:
    DISALLOW_COPY_AND_ASSIGN(ColorEntry)
};

/*
 * Check box for DockPages
 * Use Label and empty text check box for simple layout
 * Clicking the whole area should change the value
 */
class CheckBoxEntry : public QWidget {
    Q_OBJECT

public:
    CheckBoxEntry(const QString &label_text, QWidget *parent = 0);
    ~CheckBoxEntry();

    bool IsChecked() const { return check_box->isChecked(); }
    void SetChecked(bool checked) { check_box->setChecked(checked); }

protected:
    void resizeEvent(QResizeEvent *);
    void mousePressEvent(QMouseEvent *) { check_box->click(); }

private:
    Label *label;
    QCheckBox *check_box;

private slots:

signals:
    void clicked(bool);

private:
    DISALLOW_COPY_AND_ASSIGN(CheckBoxEntry)
};

/*
 * Line Entry for side by side check boxes
 */
class DualCheckBox : public QWidget {
    Q_OBJECT

public:
    DualCheckBox(const QString &left_text, const QString &right_text,
                 QWidget *parent = 0);
    ~DualCheckBox();

    bool IsLeftChecked() const { return left->IsChecked(); }
    bool IsRightChecked() const { return right->IsChecked(); }

    void SetLeftChecked(bool checked) { left->SetChecked(checked); }
    void SetRightChecked(bool checked) { right->SetChecked(checked); }

private:
    //Label *left_label, *right_label;
    //QCheckBox *left, *right;
    CheckBoxEntry *left, *right;

signals:
    void leftClicked(bool);
    void rightClicked(bool);

private:
    DISALLOW_COPY_AND_ASSIGN(DualCheckBox)
};

/*
 * Dual-Button Line Entry
 */
class DualButtonEntry : public QWidget {
    Q_OBJECT

public:
    DualButtonEntry(const QString &left_button_title,
                    const QString &right_button_title,
                    QWidget *parent = 0);
    ~DualButtonEntry();

private:
    QPushButton *left_button, *right_button;

signals:
    void leftPressed();
    void rightPressed();

private:
    DISALLOW_COPY_AND_ASSIGN(DualButtonEntry)
};

class PushButton : public QPushButton {
    Q_OBJECT

public:
    PushButton(const QString &title,
               QWidget *parent = 0)
        : QPushButton(title, parent)
    {
        setObjectName("BBPushButton");
    }
};

/*
 * Toggle button, on/off button
 */
class ToggleButton : public QPushButton {
    Q_OBJECT

public:
    ToggleButton(const QString &title,
                 QWidget *parent = 0)
        : QPushButton(title, parent)
    {
        setObjectName("BBPushButton");
        setCheckable(true);
    }
};

#endif // ENTRYWIDGETS_H
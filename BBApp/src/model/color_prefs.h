#ifndef COLOR_PREFS_H
#define COLOR_PREFS_H

#include <QSettings>
#include <QColor>
#include <QDialog>
#include "../widgets/entry_widgets.h"

// Keep everything public for now
class ColorPrefs {
public:
    ColorPrefs() { Load(); }
    ~ColorPrefs() { Save(); }

    QColor background, text, graticule, markers, markerText, limitLines;

    // Load defaults from file menu
    void LoadDefaults() {
        background = QColor(34, 40, 42);
        text = QColor(255, 255, 255);
        graticule = QColor(255, 255, 255);
        markers = QColor(255, 255, 255);
        markerText = QColor(255, 255, 255);
        limitLines = QColor(255, 0, 0);
    }

    // Printer friendly file menu load
    void LoadPrinterFriendly() {
        background = QColor(255, 255, 255);
        text = QColor(0, 0, 0);
        graticule = QColor(0, 0, 0);
        markers = QColor(0, 0, 0);
        markerText = QColor(0, 0, 0);
        limitLines = QColor(255, 0, 0);
    }

    // Load from file on startup, the default values should match LoadDefaults()
    void Load() {
        QSettings s(QSettings::IniFormat, QSettings::UserScope,
                    "SignalHound", "Preferences");

        background = s.value("ColorPrefs/Background", QColor(255, 255, 255)).value<QColor>();
        text = s.value("ColorPrefs/Text", QColor(0, 0, 0)).value<QColor>();
        graticule = s.value("ColorPrefs/Graticule", QColor(0, 0, 0)).value<QColor>();
        markers = s.value("ColorPrefs/Markers", QColor(0, 0, 0)).value<QColor>();
        markerText = s.value("ColorPrefs/MarkerText", QColor(0, 0, 0)).value<QColor>();
        limitLines = s.value("ColorPrefs/LimitLines", QColor(255, 0, 0)).value<QColor>();
    }

    // Save to file on program close
    void Save() {
        QSettings s(QSettings::IniFormat, QSettings::UserScope,
                    "SignalHound", "Preferences");

        s.setValue("ColorPrefs/Background", background);
        s.setValue("ColorPrefs/Text", text);
        s.setValue("ColorPrefs/Graticule", graticule);
        s.setValue("ColorPrefs/Markers", markers);
        s.setValue("ColorPrefs/MarkerText", markerText);
        s.setValue("ColorPrefs/LimitLines", limitLines);
    }
};

#endif // COLOR_PREFS_H

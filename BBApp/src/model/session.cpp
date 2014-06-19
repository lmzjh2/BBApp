#include "session.h"

QString Session::title;

Session::Session()
{
    device = new DeviceBB60A(&prefs);
    sweep_settings = new SweepSettings();
    trace_manager = new TraceManager();
    demod_settings = new DemodSettings();
    iq_capture = new IQCapture();
    audio_settings = new AudioSettings();

    connect(trace_manager, SIGNAL(changeCenterFrequency(Frequency)),
            sweep_settings, SLOT(setCenter(Frequency)));
    connect(trace_manager, SIGNAL(changeReferenceLevel(Amplitude)),
            sweep_settings, SLOT(setRefLevel(Amplitude)));
}

Session::~Session()
{
    delete sweep_settings;
    delete trace_manager;
    delete demod_settings;
    delete iq_capture;
    delete audio_settings;
    // Delete device last
    delete device;
}

void Session::LoadDefaults()
{
    sweep_settings->LoadDefaults();
    audio_settings->LoadDefaults();
}

void Session::LoadPreset(int p)
{
    QString fileName;
    fileName.sprintf("Preset%d", p);
    QSettings settings(QSettings::IniFormat,
                       QSettings::UserScope,
                       "SignalHound",
                       fileName);

    sweep_settings->Load(settings);
    audio_settings->Load(settings);
}

void Session::SavePreset(int p)
{
    QString fileName;
    fileName.sprintf("Preset%d", p);
    QSettings settings(QSettings::IniFormat,
                       QSettings::UserScope,
                       "SignalHound",
                       fileName);

    sweep_settings->Save(settings);
    audio_settings->Save(settings);
}

void Session::SetTitle(const QString &new_title)
{
    title = new_title;

    if(title.size() > MAX_TITLE_LEN) {
        title.resize(MAX_TITLE_LEN);
    }
}
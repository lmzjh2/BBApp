#ifndef PROGRESS_DIALOG_H
#define PROGRESS_DIALOG_H

#include <QProgressDialog>
#include <QTimer>

#include <thread>

class ProgressDialog : public QProgressDialog {
    Q_OBJECT

public:
    ProgressDialog(QWidget *parent = 0) :
        QProgressDialog("", QString(), 0, 0, parent)
    {
        //setObjectName("SH_Page");
        setWindowTitle("Initialization");
        setModal(false);
    }

    ~ProgressDialog() {}

    void makeVisible(const QString &label) {
        setLabelText(label);
        QMetaObject::invokeMethod(this, "show");
    }

    void makeDisappear() {
        QMetaObject::invokeMethod(this, "hide");
    }

private:

};

#endif // PROGRESS_DIALOG_H
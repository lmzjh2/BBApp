#ifndef CENTRAL_STACK_H
#define CENTRAL_STACK_H

#include <QStackedWidget>
#include <QToolBar>

#include "lib/frequency.h"

class Session;

class CentralWidget : public QWidget {
    Q_OBJECT

public:
    CentralWidget(QWidget *parent = 0, Qt::WindowFlags f = 0) :
        QWidget(parent, f), toolBar(0) {}
    virtual ~CentralWidget() = 0;

    virtual void GetViewImage(QImage &image) = 0;
    virtual void StartStreaming() = 0;
    virtual void StopStreaming() = 0;
    virtual void ResetView() = 0;

    virtual Frequency GetCurrentCenterFreq() const = 0;

    QToolBar* GetToolBar() const { return toolBar; }

protected:
    QToolBar *toolBar;

public slots:
    virtual void changeMode(int newState) = 0;
};

inline CentralWidget::~CentralWidget() {}


class CentralStack : public QStackedWidget {
    Q_OBJECT

public:
    CentralStack(QWidget *parent = 0) :
        QStackedWidget(parent) {}
    ~CentralStack() {}

    int AddWidget(CentralWidget *w) {
        return addWidget(w);
    }

    int InsertWidget(int index, CentralWidget *w) {
        return insertWidget(index, w);
    }

    CentralWidget* CurrentWidget() const {
        return reinterpret_cast<CentralWidget*>(currentWidget());
    }

    CentralWidget* Widget(int index) const {
        return reinterpret_cast<CentralWidget*>(widget(index));
    }
};

#endif // CENTRAL_STACK_H

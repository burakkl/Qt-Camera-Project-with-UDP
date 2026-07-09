#ifndef ENROLLDIALOG_H
#define ENROLLDIALOG_H

#include <QDialog>
#include <QList>
#include <QImage>

class FaceEngine;
class CameraFeed;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;

class EnrollDialog : public QDialog
{
    Q_OBJECT
public:
    EnrollDialog(FaceEngine *engine, const QList<CameraFeed*> &feeds,
                 QWidget *parent = nullptr);

private slots:
    void updatePreview();
    void onCapture();
    void onEnrollFinished(bool ok, const QString &name, const QString &msg);

private:
    QImage currentSelectedFrame() const;

    FaceEngine        *m_engine;
    QList<CameraFeed*> m_feeds;

    QComboBox   *m_camSelect   = nullptr;
    QLabel      *m_preview     = nullptr;
    QLineEdit   *m_nameEdit    = nullptr;
    QPushButton *m_captureBtn  = nullptr;
    QLabel      *m_status      = nullptr;
    QTimer      *m_previewTimer = nullptr;
};

#endif

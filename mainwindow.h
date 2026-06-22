#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QComboBox>
#include <QGridLayout>
#include <QTabWidget>
#include <QList>
#include <QStringList>
#include <QTimer>
#include <QMediaDevices>
#include <QCameraDevice>
#include "camerafeed.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
class QThread;
class QAction;
QT_END_NAMESPACE

class FaceEngine;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onCameraSelected(int index);
    void onCamerasChanged();       // USB takma/çıkarma

    // Yüz tanıma araç çubuğu
    void onFaceToggled(bool on);
    void onEnrollClicked();
    void onManageClicked();

private:
    Ui::MainWindow *ui;

    QComboBox          *comboBox;
    QWidget            *gridWidget;
    QGridLayout        *gridLayout;
    QTabWidget         *tabWidget;
    QMediaDevices      *mediaDevices;

    QTimer             *camChangeDebounce;

    QList<CameraFeed*>  localFeeds;   // PC yerel kameralar
    QList<CameraFeed*>  udpFeeds;     // Pi UDP kameralar
    QList<CameraFeed*>  cameraFeeds;  // hepsi birlikte (local + udp)
    QList<QWidget*>     placeholders;

    // ── Yüz tanıma ──────────────────────────────────────────────────────────
    QThread     *m_faceThread   = nullptr;
    FaceEngine  *m_faceEngine   = nullptr;
    QAction     *m_faceToggleAct = nullptr;
    QStringList  m_knownNames;            // personListReady ile güncellenir
    int          m_nextFeedId   = 0;      // her feed'e benzersiz id

    void setupFaceEngine();
    void setupToolbar();

    void refreshLocalCameras();
    void rebuildComboBox();
    void updateGrid();
    void showAll();
    void showSingle(int index);
};

#endif // MAINWINDOW_H

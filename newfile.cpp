#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QPainter>
#include <QImage>
#include <QDateTime>
#include <QThread>
#include <QDebug>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>

// QtMultimedia includes
#include <QMediaCaptureSession>
#include <QVideoFrameInput>
#include <QMediaRecorder>
#include <QMediaFormat>
#include <QVideoFrame>
#include <QUrl>

// ==========================================
// КОНСТАНТЫ НАСТРОЙКИ ВИДЕО
// ==========================================
namespace VideoConfig {
    constexpr int WIDTH            = 1280;
    constexpr int HEIGHT           = 720;
    constexpr int FPS              = 30;
    constexpr int TIMER_INTERVAL   = 1000 / FPS;
}

// ==========================================
// РАБОЧИЙ ПОТОК: формирует и отправляет кадры
// ==========================================
class CaptureWorker : public QObject {
    Q_OBJECT

public:
    explicit CaptureWorker(QObject *parent = nullptr) : QObject(parent) {
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &CaptureWorker::onTick);

        m_videoInput = new QVideoFrameInput(this);
        m_recorder   = new QMediaRecorder(this);

        m_recorder->setFileFormat(QMediaFormat::MPEG4);
        m_recorder->setVideoCodec(QMediaFormat::VideoCodec::H264);
        m_recorder->setQuality(QMediaRecorder::HighQuality);
        m_recorder->setVideoResolution(QSize(VideoConfig::WIDTH, VideoConfig::HEIGHT));

        QString fileName = "capture_" +
                           QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".mp4";
        m_recorder->setOutputLocation(QUrl::fromLocalFile(fileName));

        m_captureSession.setVideoFrameInput(m_videoInput);
        m_captureSession.setRecorder(m_recorder);

        m_canvas = QImage(VideoConfig::WIDTH, VideoConfig::HEIGHT, QImage::Format_RGB32);
    }

public slots:
    void startRecording() {
        m_recorder->record();
        m_timer->start(VideoConfig::TIMER_INTERVAL);
        qDebug() << "[Worker] Запись запущена";
    }

    void stopRecording() {
        m_timer->stop();
        m_recorder->stop();
        qDebug() << "[Worker] Запись остановлена";
    }

    // GUI-поток присылает готовый QImage с отрендеренной сценой
    void onSceneRendered(const QImage &sceneImage) {
        if (m_isBusy) return;
        m_isBusy = true;

        // Копируем отрендеренную сцену на наш холст
        m_canvas.fill(Qt::black);
        QPainter painter(&m_canvas);
        painter.drawImage(0, 0, sceneImage);

        // Дорисовываем кастомную графику
        painter.setPen(Qt::white);
        painter.setFont(QFont("Arial", 16));
        painter.drawText(50, VideoConfig::HEIGHT - 50,
                         "Время: " + QDateTime::currentDateTime().toString("hh:mm:ss.zzz"));

        painter.end();

        QVideoFrame frame(m_canvas);
        m_videoInput->sendVideoFrame(frame);

        m_isBusy = false;
    }

signals:
    void renderSceneRequested();

private:
    void onTick() {
        if (m_isBusy) return;
        emit renderSceneRequested();
    }

    QTimer               *m_timer       = nullptr;
    QImage                m_canvas;
    bool                  m_isBusy      = false;

    QMediaCaptureSession  m_captureSession;
    QVideoFrameInput     *m_videoInput  = nullptr;
    QMediaRecorder       *m_recorder    = nullptr;
};


// ==========================================
// ГЛАВНОЕ ОКНО: содержит QGraphicsScene и управляет потоком
// ==========================================
class VideoCaptureWidget : public QWidget {
    Q_OBJECT

public:
    explicit VideoCaptureWidget(QWidget *parent = nullptr) : QWidget(parent) {
        setWindowTitle("Захват QGraphicsScene в MP4 (многопоточный)");
        resize(1000, 800);

        QVBoxLayout *mainLayout = new QVBoxLayout(this);

        // ==========================================
        // Создаём QGraphicsScene и добавляем элементы
        // ==========================================
        m_scene = new QGraphicsScene(this);
        m_scene->setSceneRect(0, 0, 2000, 1500); // Большая сцена (больше видео)

        // Добавляем несколько элементов в сцену
        for (int i = 0; i < 5; ++i) {
            QGraphicsRectItem *rect = m_scene->addRect(i * 300, i * 200, 250, 150);
            rect->setBrush(QColor::fromHsv(i * 60, 200, 200));
            rect->setPen(QPen(Qt::black, 3));
        }

        QGraphicsTextItem *text = m_scene->addText("Привет, QGraphicsScene!");
        text->setPos(500, 400);
        text->setDefaultTextColor(Qt::white);
        text->setFont(QFont("Arial", 48, QFont::Bold));

        // ==========================================
        // Создаём QGraphicsView для отображения сцены
        // ==========================================
        m_view = new QGraphicsView(m_scene, this);
        m_view->setDragMode(QGraphicsView::ScrollHandDrag); // Позволяет перемещать сцену мышью
        m_view->setRenderHint(QPainter::Antialiasing);
        
        mainLayout->addWidget(m_view);

        // Кнопка управления записью
        m_recordBtn = new QPushButton("НАЧАТЬ ЗАПИСЬ", this);
        m_recordBtn->setStyleSheet("background-color: red; color: white; font-size: 18px; padding: 10px;");
        connect(m_recordBtn, &QPushButton::clicked, this, &VideoCaptureWidget::toggleRecording);
        mainLayout->addWidget(m_recordBtn);

        // ==========================================
        // Создаём рабочий поток и воркер
        // ==========================================
        m_workerThread = new QThread(this);
        m_worker       = new CaptureWorker();
        m_worker->moveToThread(m_workerThread);

        connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

        // Связи между GUI-потоком и рабочим потоком
        connect(m_worker, &CaptureWorker::renderSceneRequested,
                this,     &VideoCaptureWidget::renderSceneToImage,
                Qt::QueuedConnection);

        connect(this,        &VideoCaptureWidget::sceneRendered,
                m_worker,    &CaptureWorker::onSceneRendered,
                Qt::QueuedConnection);

        connect(this, &VideoCaptureWidget::startRequested,
                m_worker, &CaptureWorker::startRecording, Qt::QueuedConnection);
        connect(this, &VideoCaptureWidget::stopRequested,
                m_worker, &CaptureWorker::stopRecording, Qt::QueuedConnection);

        m_workerThread->start();
    }

    ~VideoCaptureWidget() {
        m_workerThread->quit();
        m_workerThread->wait();
    }

private slots:
    void toggleRecording() {
        if (m_workerThread->isRunning() && m_isRecording) {
            emit stopRequested();
            m_isRecording = false;
            m_recordBtn->setText("НАЧАТЬ ЗАПИСЬ");
            m_recordBtn->setStyleSheet("background-color: red; color: white; font-size: 18px; padding: 10px;");
        } else {
            emit startRequested();
            m_isRecording = true;
            m_recordBtn->setText("ОСТАНОВИТЬ ЗАПИСЬ");
            m_recordBtn->setStyleSheet("background-color: darkred; color: white; font-size: 18px; padding: 10px;");
        }
    }

    // Вызывается в GUI-потоке по запросу воркера
    void renderSceneToImage() {
        // Создаём QImage размером с видео
        QImage img(VideoConfig::WIDTH, VideoConfig::HEIGHT, QImage::Format_RGB32);
        img.fill(Qt::black);

        QPainter painter(&img);
        painter.setRenderHint(QPainter::Antialiasing);

        // ==========================================
        // КЛЮЧЕВОЙ МОМЕНТ: Рендерим ВСЮ сцену целиком
        // ==========================================
        // sourceRect — вся сцена (игнорируем текущий viewport QGraphicsView)
        QRectF sourceRect = m_scene->sceneRect();
        
        // targetRect — весь холст видео
        QRectF targetRect(0, 0, VideoConfig::WIDTH, VideoConfig::HEIGHT);

        // render() автоматически масштабирует сцену под targetRect
        m_scene->render(&painter, targetRect, sourceRect);

        painter.end();

        // Отправляем готовый QImage в рабочий поток
        emit sceneRendered(img);
    }

signals:
    void sceneRendered(const QImage &image);
    void startRequested();
    void stopRequested();

private:
    // QGraphicsScene и QGraphicsView (живут в GUI-потоке)
    QGraphicsScene *m_scene;
    QGraphicsView  *m_view;
    QPushButton    *m_recordBtn;

    // Рабочий поток и воркер
    QThread       *m_workerThread;
    CaptureWorker *m_worker;
    bool           m_isRecording = false;
};


int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    VideoCaptureWidget window;
    window.show();

    return app.exec();
}

#include "main.moc"
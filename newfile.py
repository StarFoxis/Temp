#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QStackedLayout>
#include <QLabel>
#include <Q3DBars>
#include <Q3DTheme>
#include <QSurfaceFormat>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QMouseEvent>
#include <QTimer>
#include <QImage>
#include <QPainter>

using namespace QtDataVisualization;

class Hybrid3DWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(float imageOpacity READ imageOpacity WRITE setImageOpacity)
    Q_PROPERTY(float graphOpacity READ graphOpacity WRITE setGraphOpacity)

public:
    explicit Hybrid3DWidget(QWidget *parent = nullptr)
        : QWidget(parent), m_isHovered(false)
    {
        // Настройка формата поверхности
        QSurfaceFormat format;
        format.setAlphaBufferSize(8);
        format.setDepthBufferSize(24);
        format.setSamples(4);
        QSurfaceFormat::setDefaultFormat(format);

        // Создаем layout для переключения между QImage и Q3DBars
        m_stackLayout = new QStackedLayout(this);
        m_stackLayout->setStackingMode(QStackedLayout::StackAll);

        // 1. QLabel для отображения QImage (прозрачный режим)
        m_imageLabel = new QLabel(this);
        m_imageLabel->setAlignment(Qt::AlignCenter);
        m_imageEffect = new QGraphicsOpacityEffect(m_imageLabel);
        m_imageEffect->setOpacity(1.0); // Начальная прозрачность 50%
        m_imageLabel->setGraphicsEffect(m_imageEffect);
        m_stackLayout->addWidget(m_imageLabel);

        // 2. Q3DBars (интерактивный режим)
        m_graph = new Q3DBars();
        m_graph->setFormat(format);
        
        // Настраиваем тему
        Q3DTheme *theme = m_graph->activeTheme();
        theme->setBackgroundEnabled(true);
        QColor bgColor = theme->backgroundColor();
        bgColor.setAlpha(255); // Непрозрачный в интерактивном режиме
        theme->setBackgroundColor(bgColor);
        
        m_graph->setShadowQuality(QAbstract3DGraph::ShadowQualityNone);

        // Встраиваем в контейнер
        m_graphContainer = QWidget::createWindowContainer(m_graph, this);
        m_graphEffect = new QGraphicsOpacityEffect(m_graphContainer);
        m_graphEffect->setOpacity(0.0); // Скрыт в начале
        m_graphContainer->setGraphicsEffect(m_graphEffect);
        m_stackLayout->addWidget(m_graphContainer);

        // Перехватчик событий для контейнера
        m_graphContainer->installEventFilter(this);

        // Анимации для плавного перехода
        m_imageAnim = new QPropertyAnimation(this, "imageOpacity", this);
        m_imageAnim->setDuration(300);
        m_imageAnim->setEasingCurve(QEasingCurve::InOutQuad);

        m_graphAnim = new QPropertyAnimation(this, "graphOpacity", this);
        m_graphAnim->setDuration(300);
        m_graphAnim->setEasingCurve(QEasingCurve::InOutQuad);

        // Таймер для периодического обновления QImage
        m_updateTimer = new QTimer(this);
        connect(m_updateTimer, &QTimer::timeout, this, &Hybrid3DWidget::updateImage);
        m_updateTimer->start(100); // Обновляем 10 раз в секунду

        // Добавляем тестовые данные
        setupTestData();

        // Первоначальный рендер в QImage
        QTimer::singleShot(100, this, &Hybrid3DWidget::updateImage);
    }

    float imageOpacity() const {
        return m_imageEffect->opacity();
    }

    void setImageOpacity(float opacity) {
        m_imageEffect->setOpacity(opacity);
    }

    float graphOpacity() const {
        return m_graphEffect->opacity();
    }

    void setGraphOpacity(float opacity) {
        m_graphEffect->setOpacity(opacity);
    }

protected:
    bool eventFilter(QObject *obj, QEvent *event) override
    {
        if (obj == m_graphContainer) {
            if (event->type() == QEvent::Enter) {
                switchToInteractive();
                return true;
            } else if (event->type() == QEvent::Leave) {
                switchToTransparent();
                return true;
            }
        }
        return QWidget::eventFilter(obj, event);
    }

private slots:
    void updateImage()
    {
        if (!m_isHovered && m_graph) {
            // Рендерим текущее состояние сцены в QImage
            QImage frame = m_graph->renderToImage(size() * devicePixelRatioF());
            
            // Применяем прозрачность 50%
            QImage transparentFrame(frame.size(), QImage::Format_ARGB32_Premultiplied);
            transparentFrame.fill(Qt::transparent);
            
            QPainter painter(&transparentFrame);
            painter.setOpacity(0.5);
            painter.drawImage(0, 0, frame);
            painter.end();
            
            m_imageLabel->setPixmap(QPixmap::fromImage(transparentFrame));
        }
    }

    void switchToInteractive()
    {
        if (m_isHovered) return;
        m_isHovered = true;
        
        m_updateTimer->stop(); // Останавливаем обновление QImage
        
        // Анимация: скрываем QImage, показываем Q3DBars
        m_imageAnim->setStartValue(1.0);
        m_imageAnim->setEndValue(0.0);
        m_imageAnim->start();

        m_graphAnim->setStartValue(0.0);
        m_graphAnim->setEndValue(1.0);
        m_graphAnim->start();
    }

    void switchToTransparent()
    {
        if (!m_isHovered) return;
        m_isHovered = false;
        
        // Анимация: показываем QImage, скрываем Q3DBars
        m_imageAnim->setStartValue(0.0);
        m_imageAnim->setEndValue(1.0);
        m_imageAnim->start();

        m_graphAnim->setStartValue(1.0);
        m_graphAnim->setEndValue(0.0);
        m_graphAnim->start();
        
        // После завершения анимации обновляем QImage и запускаем таймер
        QTimer::singleShot(350, this, [this]() {
            updateImage();
            m_updateTimer->start(100);
        });
    }

private:
    void setupTestData()
    {
        QBar3DSeries *series = new QBar3DSeries();
        
        QBarDataProxy *proxy = new QBarDataProxy();
        QBarDataRow *row1 = new QBarDataRow();
        *row1 << QBarDataItem(1.0f) << QBarDataItem(2.0f) << QBarDataItem(3.0f);
        proxy->addRow(row1);
        
        QBarDataRow *row2 = new QBarDataRow();
        *row2 << QBarDataItem(2.5f) << QBarDataItem(1.5f) << QBarDataItem(2.0f);
        proxy->addRow(row2);
        
        series->setDataProxy(proxy);
        m_graph->addSeries(series);
    }

    QStackedLayout *m_stackLayout;
    QLabel *m_imageLabel;
    QWidget *m_graphContainer;
    Q3DBars *m_graph;
    QGraphicsOpacityEffect *m_imageEffect;
    QGraphicsOpacityEffect *m_graphEffect;
    QPropertyAnimation *m_imageAnim;
    QPropertyAnimation *m_graphAnim;
    QTimer *m_updateTimer;
    bool m_isHovered;
};

// Главное окно для демонстрации
class MainWindow : public QWidget
{
public:
    explicit MainWindow(QWidget *parent = nullptr) : QWidget(parent)
    {
        setWindowTitle("Гибридный 3D-виджет");
        resize(800, 600);

        QVBoxLayout *mainLayout = new QVBoxLayout(this);

        // Фоновый виджет
        QWidget *backgroundWidget = new QWidget(this);
        backgroundWidget->setStyleSheet(
            "background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1, "
            "stop:0 #3498db, stop:1 #2ecc71);"
        );
        
        QVBoxLayout *bgLayout = new QVBoxLayout(backgroundWidget);
        QLabel *label = new QLabel("Фоновый виджет\n(Наведите мышь на 3D-график)");
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet("color: white; font-size: 24px; font-weight: bold;");
        bgLayout->addWidget(label);

        // Гибридный 3D-виджет
        Hybrid3DWidget *hybrid3D = new Hybrid3DWidget(this);

        // Наложение
        QWidget *overlayContainer = new QWidget(this);
        QStackedLayout *stackLayout = new QStackedLayout(overlayContainer);
        stackLayout->setStackingMode(QStackedLayout::StackAll);
        
        stackLayout->addWidget(backgroundWidget);
        stackLayout->addWidget(hybrid3D);

        mainLayout->addWidget(overlayContainer);
    }
};

int main(int argc, char *argv[])
{
    QSurfaceFormat format;
    format.setAlphaBufferSize(8);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    MainWindow window;
    window.show();
    return app.exec();
}
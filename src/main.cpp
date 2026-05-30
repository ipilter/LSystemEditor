#include <cuda_runtime.h>

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include <QString>

namespace {

QString formatGpuInfo()
{
    int deviceCount = 0;
    const cudaError_t countError = cudaGetDeviceCount(&deviceCount);
    if (countError != cudaSuccess) {
        return QStringLiteral("CUDA error: %1").arg(cudaGetErrorString(countError));
    }

    if (deviceCount <= 0) {
        return QStringLiteral("No CUDA-capable GPU found.");
    }

    cudaDeviceProp properties{};
    const cudaError_t propError = cudaGetDeviceProperties(&properties, 0);
    if (propError != cudaSuccess) {
        return QStringLiteral("CUDA error: %1").arg(cudaGetErrorString(propError));
    }

    int clockRateKHz = 0;
    cudaDeviceGetAttribute(&clockRateKHz, cudaDevAttrClockRate, 0);

    const double totalMemoryGiB =
        static_cast<double>(properties.totalGlobalMem) / (1024.0 * 1024.0 * 1024.0);

    return QStringLiteral(
               "GPU: %1\n"
               "Compute capability: %2.%3\n"
               "Total global memory: %4 GiB\n"
               "Multiprocessors: %5\n"
               "Clock rate: %6 MHz")
        .arg(properties.name)
        .arg(properties.major)
        .arg(properties.minor)
        .arg(totalMemoryGiB, 0, 'f', 2)
        .arg(properties.multiProcessorCount)
        .arg(clockRateKHz / 1000);
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QWidget window;
    window.setWindowTitle(QStringLiteral("PathTracer"));
    window.resize(480, 200);

    auto* layout = new QVBoxLayout(&window);

    auto* infoLabel = new QLabel(formatGpuInfo(), &window);
    infoLabel->setWordWrap(true);
    layout->addWidget(infoLabel);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addStretch();

    auto* closeButton = new QPushButton(QStringLiteral("Close"), &window);
    QObject::connect(closeButton, &QPushButton::clicked, &window, &QWidget::close);
    buttonRow->addWidget(closeButton);

    layout->addLayout(buttonRow);

    window.show();
    return app.exec();
}

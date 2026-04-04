Since I am modifying multiple areas in `MainWindow.cpp` to correctly apply tooltips and truncate the text, creating a helper method `updateModelLabel` makes the most sense.

```cpp
void MainWindow::updateModelUI(const QString& context) {
    if (!modelLabel || !modelSelectButton) return;

    QString labelText;
    QString tooltipText;

    if (m_selectedModels.size() > 1) {
        labelText = tr("Model: %1 Models (%2)").arg(m_selectedModels.size()).arg(context);
        tooltipText = m_selectedModels.join("\n");
        modelSelectButton->setText(tr("%1 Models Selected").arg(m_selectedModels.size()));
    } else {
        QString model = m_selectedModel;
        tooltipText = model;
        if (model.length() > 20) {
            model = model.left(17) + "...";
        }
        labelText = tr("Model: %1 (%2)").arg(model).arg(context);
        modelSelectButton->setText(model);
    }

    modelLabel->setText(labelText);
    modelLabel->setToolTip(tooltipText);
    modelSelectButton->setToolTip(tooltipText);
}
```

Let's declare `void updateModelUI(const QString& context);` in `src/MainWindow.h`.

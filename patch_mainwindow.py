import re

with open("src/MainWindow.cpp", "r") as f:
    content = f.read()

target = """                    QComboBox* modelCombo = new QComboBox(&dialog);
                    for (const auto& m : ollamaClient.availableModels()) {
                        modelCombo->addItem(m.name, m.name);
                    }"""

replacement = """                    QComboBox* modelCombo = new QComboBox(&dialog);
                    for (const auto& m : m_availableModels) {
                        modelCombo->addItem(m, m);
                    }"""

if target in content:
    content = content.replace(target, replacement)
    with open("src/MainWindow.cpp", "w") as f:
        f.write(content)
    print("Patched.")
else:
    print("Target not found.")

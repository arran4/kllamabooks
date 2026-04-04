import re

with open("src/MainWindow.cpp", "r") as f:
    content = f.read()

content = content.replace("""                                    documentEditorView->blockSignals(false);
            }
            break;
        }
    }
    }
    }


    // Find book index""", """                    documentEditorView->blockSignals(false);
                }
                break;
            }
        }
    }

    // Find book index""")

with open("src/MainWindow.cpp", "w") as f:
    f.write(content)

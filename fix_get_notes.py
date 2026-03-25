import re

def process_file(filepath):
    with open(filepath, 'r') as file:
        data = file.read()

    data = data.replace('ChatSettingsDialog::getNotes() const', 'ChatSettingsDialog::getComments() const')

    with open(filepath, 'w') as file:
        file.write(data)

process_file("src/ChatSettingsDialog.cpp")
process_file("src/ChatSettingsDialog.h")

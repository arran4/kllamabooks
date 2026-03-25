import re

def process_file(filepath):
    with open(filepath, 'r') as file:
        data = file.read()

    data = data.replace('m_newChatNotes', 'm_newChatComments')
    data = data.replace('m_notesEdit', 'm_commentsEdit')
    data = data.replace('QString getNotes() const', 'QString getComments() const')
    data = data.replace('dlg.getNotes()', 'dlg.getComments()')
    data = data.replace('initialNotes', 'initialComments')
    data = data.replace('newNotes', 'newComments')
    data = data.replace('currentNotes', 'currentComments')
    data = data.replace('tr("Notes:")', 'tr("Comments:")')
    data = data.replace('Optional notes for this chat/fork...', 'Optional comments for this chat/fork...')

    # Replace DB setting strings
    data = data.replace('("chat", messageId, "notes"', '("chat", messageId, "comments"')
    data = data.replace('("chat", userMsgId, "notes"', '("chat", userMsgId, "comments"')
    data = data.replace(', "notes", new', ', "comments", new')

    with open(filepath, 'w') as file:
        file.write(data)

process_file("src/ChatSettingsDialog.cpp")
process_file("src/ChatSettingsDialog.h")
process_file("src/MainWindow.cpp")
process_file("src/MainWindow.h")

import re

with open("src/MergeDocumentsDialog.h", "r") as f:
    header = f.read()

if "bool m_parseTemplate = true;" not in header:
    search = "    QString buildPreviewPrompt() const;"
    replace = "    QString buildPreviewPrompt() const;\n    bool m_parseTemplate = true;"
    header = header.replace(search, replace)

    search2 = "    void onSaveTemplateClicked();"
    replace2 = "    void onSaveTemplateClicked();\n    void onParseTemplateToggled(int state);"
    header = header.replace(search2, replace2)

    with open("src/MergeDocumentsDialog.h", "w") as f:
        f.write(header)

with open("src/MergeDocumentsDialog.cpp", "r") as f:
    content = f.read()

if "Parse as Template" not in content:
    search1 = "    layout->addLayout(m_dynamicInputsLayout);"
    replace1 = """    layout->addLayout(m_dynamicInputsLayout);

    QCheckBox* parseTemplateCheck = new QCheckBox(tr("Parse as Template (Uncheck if regenerating previously outputted prompt to prevent double parsing)"), this);
    parseTemplateCheck->setChecked(true);
    m_parseTemplate = true;
    layout->addWidget(parseTemplateCheck);
    connect(parseTemplateCheck, &QCheckBox::stateChanged, this, &MergeDocumentsDialog::onParseTemplateToggled);"""

    content = content.replace(search1, replace1)

    search2 = """void MergeDocumentsDialog::onSaveTemplateClicked() {"""
    replace2 = """void MergeDocumentsDialog::onParseTemplateToggled(int state) {
    m_parseTemplate = (state == Qt::Checked);
}

void MergeDocumentsDialog::onSaveTemplateClicked() {"""
    content = content.replace(search2, replace2)

    search3 = """    return TemplateParser::parseMergeTemplate(prompt, m_documentContents);"""
    replace3 = """    if (m_parseTemplate) {
        return TemplateParser::parseMergeTemplate(prompt, m_documentContents);
    }
    return prompt;"""
    content = content.replace(search3, replace3)

    with open("src/MergeDocumentsDialog.cpp", "w") as f:
        f.write(content)

#ifndef TEMPLATEMODEL_H
#define TEMPLATEMODEL_H

#include <QString>

enum class TemplateSource { BuiltIn, Global, Book, ExternalCache };

enum class TemplatePurpose { DocumentCreation, SingleDocTransform, MultiDocMerge, ChatSystemPrompt };

inline QString templateSourceToString(TemplateSource source) {
    switch (source) {
        case TemplateSource::BuiltIn:
            return QStringLiteral("built-in");
        case TemplateSource::Global:
            return QStringLiteral("global");
        case TemplateSource::Book:
            return QStringLiteral("database");
        case TemplateSource::ExternalCache:
            return QStringLiteral("external-cache");
    }
    return QStringLiteral("built-in");
}

inline TemplateSource templateSourceFromString(const QString& value) {
    const QString norm = value.trimmed().toLower();
    if (norm == QLatin1String("global")) return TemplateSource::Global;
    if (norm == QLatin1String("database") || norm == QLatin1String("book")) return TemplateSource::Book;
    if (norm == QLatin1String("external-cache") || norm == QLatin1String("external")) {
        return TemplateSource::ExternalCache;
    }
    return TemplateSource::BuiltIn;
}

inline bool isTemplateSourceReadOnly(TemplateSource source) {
    return source == TemplateSource::BuiltIn || source == TemplateSource::ExternalCache;
}

inline QString templatePurposeToString(TemplatePurpose purpose) {
    switch (purpose) {
        case TemplatePurpose::DocumentCreation:
            return QStringLiteral("document_creation");
        case TemplatePurpose::SingleDocTransform:
            return QStringLiteral("single_doc_transform");
        case TemplatePurpose::MultiDocMerge:
            return QStringLiteral("multi_doc_merge");
        case TemplatePurpose::ChatSystemPrompt:
            return QStringLiteral("chat_system_prompt");
    }
    return QStringLiteral("document_creation");
}

inline TemplatePurpose templatePurposeFromString(const QString& value) {
    const QString norm = value.trimmed().toLower();
    if (norm == QLatin1String("multiple") || norm == QLatin1String("multi_doc_merge")) {
        return TemplatePurpose::MultiDocMerge;
    }
    if (norm == QLatin1String("single") || norm == QLatin1String("single_doc_transform")) {
        return TemplatePurpose::SingleDocTransform;
    }
    if (norm == QLatin1String("chat") || norm == QLatin1String("chat_system_prompt")) {
        return TemplatePurpose::ChatSystemPrompt;
    }
    return TemplatePurpose::DocumentCreation;
}

#endif  // TEMPLATEMODEL_H

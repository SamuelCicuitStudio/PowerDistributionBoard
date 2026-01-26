#pragma once

#include <Arduino.h>
#include <ConfigNVS.hpp>
#include <NVSManager.hpp>
#include <WifiEnpoin.hpp>

namespace WiFiLang {

enum class UiLanguage : uint8_t {
    English = 0,
    French,
    Italian
};

inline String normalizeLanguageCode(String raw) {
    raw.trim();
    raw.toLowerCase();
    if (raw == "fr" || raw == "francais" || raw == "french") {
        return "fr";
    }
    if (raw == "it" || raw == "italian" || raw == "italien" || raw == "italiano") {
        return "it";
    }
    if (raw == "en" || raw == "english") {
        return "en";
    }
    return String(DEFAULT_UI_LANGUAGE);
}

inline UiLanguage parseLanguage(const String& raw) {
    const String code = normalizeLanguageCode(raw);
    if (code == "fr") return UiLanguage::French;
    if (code == "it") return UiLanguage::Italian;
    return UiLanguage::English;
}

inline const char* languageCode(UiLanguage lang) {
    switch (lang) {
        case UiLanguage::French:  return "fr";
        case UiLanguage::Italian: return "it";
        default:                  return "en";
    }
}

inline UiLanguage getCurrentLanguage() {
    if (!CONF) return UiLanguage::English;
    const String v = CONF->GetString(UI_LANGUAGE_KEY, DEFAULT_UI_LANGUAGE);
    return parseLanguage(v);
}

inline const char* plainError(UiLanguage lang) {
    switch (lang) {
        case UiLanguage::French:  return "erreur";
        case UiLanguage::Italian: return "errore";
        default:                  return "error";
    }
}

inline const char* getPlainError() {
    return plainError(getCurrentLanguage());
}

struct ErrorTranslation {
    const char* key;
    const char* fr;
    const char* it;
};

static constexpr ErrorTranslation kErrorTranslations[] = {
    { ERR_ALREADY_CONNECTED,        "Deja connecte",                          "Gia connesso" },
    { ERR_INVALID_CBOR,             "CBOR invalide",                           "CBOR non valido" },
    { ERR_INVALID_ACTION,           "Action invalide",                         "Azione non valida" },
    { ERR_INVALID_ACTION_TARGET,    "Action ou cible invalide",                "Azione o destinazione non valida" },
    { ERR_MISSING_FIELDS,           "Champs manquants",                        "Campi mancanti" },
    { ERR_NOT_AUTHENTICATED,        "Non authentifie",                         "Non autenticato" },
    { ERR_UNKNOWN_TARGET,           "Cible inconnue",                          "Destinazione sconosciuta" },
    { ERR_ALLOC_FAILED,             "Echec allocation",                        "Allocazione fallita" },
    { ERR_ALREADY_RUNNING,          "Deja en cours",                           "Gia in esecuzione" },
    { ERR_BAD_PASSWORD,             "Mot de passe incorrect",                  "Password errata" },
    { ERR_BUS_SAMPLER_MISSING,      "Echantillonneur bus manquant",            "Campionatore bus mancante" },
    { ERR_CALIBRATION_BUSY,         "Calibration en cours",                    "Calibrazione in corso" },
    { ERR_CALIBRATION_FAILED,       "Echec calibration",                       "Calibrazione fallita" },
    { ERR_CTRL_QUEUE_FULL,          "File de commande pleine",                 "Coda comandi piena" },
    { ERR_DEVICE_MISSING,           "Appareil manquant",                       "Dispositivo mancante" },
    { ERR_DEVICE_NOT_IDLE,          "Appareil non au repos",                   "Dispositivo non in idle" },
    { ERR_DEVICE_TRANSPORT_MISSING, "Transport appareil manquant",             "Trasporto dispositivo mancante" },
    { ERR_ENERGY_START_FAILED,      "Demarrage energie echoue",                "Avvio energia fallito" },
    { ERR_ENERGY_STOPPED,           "Energie arretee",                         "Energia arrestata" },
    { ERR_FIT_FAILED,               "Ajustement echoue",                       "Adattamento fallito" },
    { ERR_INVALID_COEFFS,           "Coefficients invalides",                  "Coefficienti non validi" },
    { ERR_INVALID_MODE,             "Mode invalide",                           "Modalita non valida" },
    { ERR_INVALID_NAME,             "Nom invalide",                            "Nome non valido" },
    { ERR_INVALID_REF_TEMP,         "Temperature de reference invalide",       "Temperatura di riferimento non valida" },
    { ERR_INVALID_TARGET,           "Cible invalide",                          "Obiettivo non valido" },
    { ERR_MISSING_NAME,             "Nom manquant",                            "Nome mancante" },
    { ERR_NOT_ENOUGH_SAMPLES,       "Pas assez d'echantillons",                "Campioni insufficienti" },
    { ERR_NOT_FOUND,                "Introuvable",                             "Non trovato" },
    { ERR_NTC_MISSING,              "NTC manquante",                           "NTC mancante" },
    { ERR_PERSIST_FAILED,           "Echec sauvegarde",                        "Salvataggio fallito" },
    { ERR_FAILED,                   "Echec",                                  "Fallito" },
    { ERR_SENSOR_MISSING,           "Capteur manquant",                        "Sensore mancante" },
    { ERR_SETUP_INCOMPLETE,         "Configuration incomplete",                "Configurazione incompleta" },
    { ERR_SETUP_REQUIRED,           "Configuration requise",                   "Configurazione richiesta" },
    { ERR_SNAPSHOT_BUSY,            "Instantane occupe",                       "Snapshot occupato" },
    { ERR_START_FAILED,             "Demarrage echoue",                        "Avvio fallito" },
    { ERR_STATUS_UNAVAILABLE,       "Statut indisponible",                     "Stato non disponibile" },
    { ERR_STOPPED,                  "Arrete",                                 "Fermato" },
    { ERR_TASK_FAILED,              "Tache echouee",                           "Attivita fallita" },
    { ERR_TIMEOUT,                  "Delai depasse",                           "Timeout" },
    { ERR_WIRE_ACCESS_BLOCKED,      "Acces fil bloque",                        "Accesso filo bloccato" },
    { ERR_WIRE_NOT_CONNECTED,       "Fil non connecte",                        "Filo non connesso" },
    { ERR_WIRE_SUBSYSTEM_MISSING,   "Sous-systeme fil manquant",                "Sottosistema filo mancante" }
};

inline const char* translateErrorMessage(const char* message, UiLanguage lang) {
    if (!message || !message[0]) return message;
    if (lang == UiLanguage::English) return message;
    for (const auto& t : kErrorTranslations) {
        if (strcmp(message, t.key) == 0) {
            return (lang == UiLanguage::French) ? t.fr : t.it;
        }
    }
    return message;
}

struct ReasonTranslation {
    const char* en;
    const char* fr;
    const char* it;
};

static constexpr ReasonTranslation kReasonTranslations[] = {
    { "Setup incomplete",                     "Configuration incomplete",                  "Configurazione incompleta" },
    { "Stop requested",                       "Arret demande",                              "Arresto richiesto" },
    { "Idle requested",                       "Repos demande",                              "Idle richiesto" },
    { "Targeted run stopped",                 "Execution ciblee arretee",                   "Esecuzione mirata fermata" },
    { "Wire not present",                     "Fil non present",                            "Filo non presente" },
    { "Target temp invalid",                  "Temperature cible invalide",                 "Temperatura obiettivo non valida" },
    { "Floor target unset",                   "Cible du sol non definie",                   "Obiettivo pavimento non impostato" },
    { "NTC invalid",                          "NTC invalide",                               "NTC non valido" },
    { "Floor NTC invalid",                    "NTC sol invalide",                           "NTC pavimento non valido" },
    { "No wires present",                     "Aucun fil present",                          "Nessun filo presente" },
    { "12V not detected within 10s of start", "12V non detecte dans les 10 s au demarrage", "12V non rilevato entro 10 s dall'avvio" },
    { "Run preparation aborted",              "Preparation d'execution annulee",           "Preparazione esecuzione annullata" },
    { "12V supply lost during run",           "Alimentation 12V perdue pendant l'execution","Alimentazione 12V persa durante l'esecuzione" },
    { "Over-current trip",                    "Declenchement surintensite",                 "Intervento sovracorrente" },
    { "Physical sensor over-temp",            "Surchauffe capteur physique",                "Sovratemperatura sensore fisico" },
    { "Calibration aborted",                  "Calibration annulee",                        "Calibrazione annullata" },
    { "Calibration timeout (charging caps)",  "Delai calibration (charge condensateurs)",   "Timeout calibrazione (carica condensatori)" },
    { "Calibration aborted (power/watch stop)","Calibration annulee (arret securite)",      "Calibrazione annullata (stop sicurezza)" },
    { "Calibration timeout (current sensor)", "Delai calibration (capteur courant)",        "Timeout calibrazione (sensore corrente)" },
    { "Calibration timeout (capacitance)",    "Delai calibration (capacitance)",            "Timeout calibrazione (capacita)" },
    { "Calibration timeout (recharge)",       "Delai calibration (recharge)",               "Timeout calibrazione (ricarica)" },
    { "Capacitance calibration failed",       "Echec calibration capacite",                 "Calibrazione capacita fallita" },
    { "model_cal",                            "calibration modele",                         "calibrazione modello" },
    { "ntc_cal",                              "calibration NTC",                            "calibrazione NTC" },
    { "floor_cal",                            "calibration sol",                            "calibrazione pavimento" },
    { "run",                                  "marche",                                     "esecuzione" },
    { "confirmed",                            "confirme",                                   "confermato" },
    { "none",                                 "aucun",                                      "nessuno" }
};

inline bool applyPrefixTranslation(String& text,
                                   const char* enPrefix,
                                   const char* frPrefix,
                                   const char* itPrefix,
                                   UiLanguage lang) {
    if (!text.startsWith(enPrefix)) return false;
    const char* repl = (lang == UiLanguage::French) ? frPrefix : itPrefix;
    text.replace(enPrefix, repl);
    return true;
}

inline String translateReason(const char* reason, UiLanguage lang) {
    if (!reason || !reason[0]) return String();
    if (lang == UiLanguage::English) return String(reason);

    String text(reason);
    String prefix;
    int bracketEnd = text.indexOf("] ");
    if (text.startsWith("[") && bracketEnd > 0) {
        prefix = text.substring(0, bracketEnd + 2);
        text = text.substring(bracketEnd + 2);
    }

    for (const auto& t : kReasonTranslations) {
        if (text == t.en) {
            return prefix + ((lang == UiLanguage::French) ? t.fr : t.it);
        }
    }

    if (applyPrefixTranslation(text, "Temp warning sensor",
                               "Alerte temperature capteur",
                               "Avviso temperatura sensore", lang)) {
        return prefix + text;
    }
    if (applyPrefixTranslation(text, "Overtemp trip sensor",
                               "Surchauffe capteur",
                               "Sovratemperatura sensore", lang)) {
        return prefix + text;
    }
    if (applyPrefixTranslation(text, "12V lost",
                               "12V perdu",
                               "12V perso", lang)) {
        return prefix + text;
    }
    if (applyPrefixTranslation(text, "Over-current trip",
                               "Declenchement surintensite",
                               "Intervento sovracorrente", lang)) {
        return prefix + text;
    }
    if (applyPrefixTranslation(text, "Physical sensor over-temp",
                               "Surchauffe capteur physique",
                               "Sovratemperatura sensore fisico", lang)) {
        return prefix + text;
    }
    if (applyPrefixTranslation(text, "Current sampling stalled",
                               "Echantillonnage courant bloque",
                               "Campionamento corrente bloccato", lang)) {
        return prefix + text;
    }
    if (applyPrefixTranslation(text, "Calibration timeout",
                               "Delai calibration",
                               "Timeout calibrazione", lang)) {
        return prefix + text;
    }

    return prefix + text;
}

} // namespace WiFiLang

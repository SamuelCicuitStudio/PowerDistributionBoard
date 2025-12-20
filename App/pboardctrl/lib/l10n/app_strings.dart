import 'dart:async';

import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

class AppLanguageController extends ChangeNotifier {
  AppLanguageController({Locale? initialLocale})
    : _locale = _normalize(initialLocale);

  static const _prefsKey = 'language_code';

  Locale _locale;

  static Future<AppLanguageController> load({Locale? fallbackLocale}) async {
    final prefs = await SharedPreferences.getInstance();
    final storedCode = prefs.getString(_prefsKey)?.trim();
    final initialLocale = storedCode == null || storedCode.isEmpty
        ? fallbackLocale
        : Locale(storedCode);
    return AppLanguageController(initialLocale: initialLocale);
  }

  Locale get locale => _locale;

  void setLocale(Locale locale) {
    final normalized = _normalize(locale);
    if (normalized == _locale) return;
    _locale = normalized;
    notifyListeners();
    unawaited(_persistLocale(normalized));
  }

  void setLanguageCode(String code) {
    setLocale(Locale(code));
  }

  Future<void> _persistLocale(Locale locale) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_prefsKey, locale.languageCode.toLowerCase());
  }

  static Locale _normalize(Locale? locale) {
    if (locale != null && locale.languageCode.toLowerCase() == 'it') {
      return const Locale('it');
    }
    return const Locale('en');
  }
}

class AppLanguageScope extends InheritedNotifier<AppLanguageController> {
  const AppLanguageScope({
    super.key,
    required AppLanguageController controller,
    required Widget child,
  }) : super(notifier: controller, child: child);

  static AppLanguageController of(BuildContext context) {
    final scope = context
        .dependOnInheritedWidgetOfExactType<AppLanguageScope>();
    if (scope == null) {
      throw StateError('AppLanguageScope not found in widget tree.');
    }
    return scope.notifier!;
  }
}

class AppStrings {
  AppStrings(this.locale);

  final Locale locale;

  bool get isItalian => locale.languageCode.toLowerCase() == 'it';

  static const Map<String, String> _it = {
    'Language': 'Lingua',
    'English': 'Inglese',
    'Italian': 'Italiano',
    'Choose Connection': 'Scegli connessione',
    'Select how you want to connect': 'Seleziona come vuoi connetterti',
    'Pick a Wi-Fi network, connect, then log in to the admin console.':
        'Scegli una rete Wi-Fi, connettiti, poi accedi alla console admin.',
    'Available networks': 'Reti disponibili',
    'Refresh': 'Aggiorna',
    'No Wi-Fi networks found.': 'Nessuna rete Wi-Fi trovata.',
    'Not connected': 'Non connesso',
    'Connected to {ssid}': 'Connesso a {ssid}',
    'Disconnect': 'Disconnetti',
    'Connection mode': 'Modalita connessione',
    'AP Mode': 'Modalita AP',
    'Station Mode': 'Modalita Stazione',
    'Base URL: {url}': 'URL base: {url}',
    'Selected network': 'Rete selezionata',
    'None': 'Nessuna',
    'Wi-Fi password': 'Password Wi-Fi',
    'Connect & Continue': 'Connetti e continua',
    'Continue without switching Wi-Fi': 'Continua senza cambiare Wi-Fi',
    'Tip: default AP password is 1234567890.':
        'Suggerimento: password AP predefinita 1234567890.',
    'Custom address': 'Indirizzo personalizzato',
    'Enter an address or IP.': 'Inserisci un indirizzo o IP.',
    'Connect': 'Connetti',
    'Select a Wi-Fi network.': 'Seleziona una rete Wi-Fi.',
    'Wi-Fi password must be at least 8 characters.':
        'La password Wi-Fi deve avere almeno 8 caratteri.',
    'Failed to disconnect from current Wi-Fi.':
        'Impossibile disconnettersi dal Wi-Fi corrente.',
    'Disconnect did not complete. Try again.':
        'Disconnessione non completata. Riprova.',
    'Still not connected to {ssid}. Check the password and signal.':
        'Ancora non connesso a {ssid}. Controlla password e segnale.',
    'Failed to disconnect.': 'Disconnessione fallita.',
    'Failed to forget network.': 'Impossibile dimenticare la rete.',
    'Forgot network: {ssid}': 'Rete dimenticata: {ssid}',
    'Forget network': 'Dimentica rete',
    'Connected': 'Connesso',
    'Signal {signal}%': 'Segnale {signal}%',
    '(hidden network)': '(rete nascosta)',
    'On this platform, connect to the board using your system Wi-Fi settings, then continue to login.':
        'Su questa piattaforma, connettiti alla scheda dalle impostazioni Wi-Fi di sistema, poi continua al login.',
    'Login': 'Accesso',
    'PowerBoard Admin': 'PowerBoard Admin',
    'Username': 'Nome utente',
    'Password': 'Password',
    'Enter both username and password.': 'Inserisci username e password.',
    'Login failed.': 'Accesso fallito.',
    'Only Admin mode is supported in this app.':
        'Solo modalita Admin e supportata in questa app.',
    'Connection timed out.': 'Tempo scaduto per la connessione.',
    'Unable to reach the device.': 'Impossibile raggiungere il dispositivo.',
    'Login failed: {err}': 'Accesso fallito: {err}',
    'Change connection': 'Cambia connessione',
    'Menu': 'Menu',
    'Wi-Fi signal': 'Segnale Wi-Fi',
    'Board temperature': 'Temperatura scheda',
    'Heatsink temperature': 'Temperatura dissipatore',
    'DC bus voltage': 'Tensione bus DC',
    'DC current': 'Corrente DC',
    'Warnings': 'Avvisi',
    'Errors': 'Errori',
    'Change Connection': 'Cambia connessione',
    'Reset device?': 'Reset dispositivo?',
    'This performs a full system reset.':
        'Esegue un reset completo del sistema.',
    'Cancel': 'Annulla',
    'Reset': 'Reset',
    'Dismiss': 'Chiudi',
    'Command failed: {err}': 'Comando fallito: {err}',
    'Saved': 'Salvato',
    'Mute': 'Muto',
    'Unmute': 'Riattiva audio',
    'Reboot': 'Riavvia',
    'Power': 'Alimentazione',
    'Auto': 'Auto',
    'Manual': 'Manuale',
    'Ready': 'Pronto',
    'OFF': 'SPENTO',
    'IDLE': 'ATTESA',
    'ERROR': 'ERRORE',
    'RUN': 'MARCIA',
    'Dashboard': 'Dashboard',
    'User Settings': 'Utente',
    'Manual Control': 'Manuale',
    'Admin Settings': 'Admin',
    'Device Settings': 'Dispositivo',
    'Live': 'Live',
    'Voltage': 'Tensione',
    'Current': 'Corrente',
    'ADC raw (/100): {value}': 'ADC grezzo (/100): {value}',
    'Board 01': 'Scheda 01',
    'Board 02': 'Scheda 02',
    'Heatsink': 'Dissipatore',
    'Temp {index}': 'Temp {index}',
    'Off': 'Spento',
    'Capacitance': 'Capacita',
    'Force Calibration': 'Forza calibrazione',
    'Current Password': 'Password corrente',
    'New Password': 'Nuova password',
    'New Device ID': 'Nuovo ID dispositivo',
    'Output Access': 'Accesso uscite',
    'Output {index}': 'Uscita {index}',
    'Save': 'Salva',
    'Fan': 'Ventola',
    'Relay': 'Rele',
    'New Admin Username': 'Nuovo username admin',
    'New Admin Password': 'Nuova password admin',
    'Wi-Fi SSID': 'SSID Wi-Fi',
    'Wi-Fi Password': 'Password Wi-Fi',
    'Sampling & Power': 'Campionamento e potenza',
    'Sampling Rate': 'Frequenza campionamento',
    'Charge Resistor': 'Resistenza di carica',
    'Current Trip Limit': 'Limite corrente',
    'Thermal Safety': 'Sicurezza termica',
    'Temp Warning': 'Avviso temperatura',
    'Temp Trip (Shutdown)': 'Soglia temp (spegnimento)',
    'Thermal Model': 'Modello termico',
    'Idle Current Baseline': 'Corrente idle base',
    'Wire Tau (time constant)': 'Tau filo (costante di tempo)',
    'Heat Loss k': 'Perdita calore k',
    'Thermal Mass C': 'Massa termica C',
    'PI Control': 'Controllo PI',
    'Wire Kp': 'Kp filo',
    'Wire Ki': 'Ki filo',
    'Floor Kp': 'Kp pavimento',
    'Floor Ki': 'Ki pavimento',
    'NTC Input': 'Ingresso NTC',
    'NTC Beta': 'Beta NTC',
    'NTC R0': 'R0 NTC',
    'NTC Fixed Resistor': 'Resistenza fissa NTC',
    'NTC Min Temp': 'Temp minima NTC',
    'NTC Max Temp': 'Temp massima NTC',
    'NTC Samples': 'Campioni NTC',
    'NTC Wire Index': 'Indice filo NTC',
    'Button Press Threshold': 'Soglia pressione pulsante',
    'Button Release Threshold': 'Soglia rilascio pulsante',
    'Button Debounce': 'Debounce pulsante',
    'Loop Timing': 'Temporizzazione ciclo',
    'ON Time': 'Tempo ON',
    'OFF Time': 'Tempo OFF',
    'Loop Mode': 'Modalita ciclo',
    'Advanced (grouped)': 'Avanzato (raggruppato)',
    'Sequential (coolest-first)': 'Sequenziale (piu freddo prima)',
    'Mixed (preheat + keep-warm, sequential pulses)':
        'Misto (preheat + mantenimento, impulsi sequenziali)',
    'Mixed Preheat Duration': 'Durata preheat misto',
    'Preheat Pulse (primary)': 'Impulso preheat (primario)',
    'Keep-warm Pulse (others)': 'Impulso mantenimento (altri)',
    'Frame Period': 'Periodo frame',
    'Timing Settings': 'Impostazioni timing',
    'Normal (Hot / Medium / Gentle)': 'Normale (Caldo / Medio / Delicato)',
    'Advanced (manual ON/OFF)': 'Avanzato (manuale ON/OFF)',
    'Heat Profile': 'Profilo calore',
    'Hot / Fast': 'Caldo / Veloce',
    'Medium': 'Medio',
    'Gentle / Cool': 'Delicato / Freddo',
    'Resolved Timing': 'Timing risolto',
    'Wire Gauge (AWG)': 'Calibro filo (AWG)',
    'Nichrome Calibration': 'Calibrazione nichrome',
    'Floor Settings': 'Impostazioni pavimento',
    'Floor Thickness (20-50 mm)': 'Spessore pavimento (20-50 mm)',
    'Nichrome Final Temp': 'Temp finale nichrome',
    'Floor Material': 'Materiale pavimento',
    'Wood': 'Legno',
    'Epoxy': 'Epoxy',
    'Concrete': 'Cemento',
    'Slate': 'Ardesia',
    'Marble': 'Marmo',
    'Granite': 'Granito',
    'Max Floor Temp (<= 35 C)': 'Temp massima pavimento (<= 35 C)',
    'Wire Resistivity (Ohm/m)': 'Resistivita filo (Ohm/m)',
    'Target Resistance': 'Resistenza target',
    'No active session': 'Nessuna sessione attiva',
    'Running': 'In corso',
    'Last session': 'Ultima sessione',
    'ON (energized)': 'ON (attivo)',
    'OFF (connected)': 'OFF (connesso)',
    'No wire / Not connected': 'Nessun filo / Non connesso',
    'Current Session': 'Sessione corrente',
    'Energy': 'Energia',
    'Duration': 'Durata',
    'Peak Power': 'Potenza max',
    'Peak Current': 'Corrente max',
    'History': 'Storico',
    'Calibration': 'Calibrazione',
    'Error': 'Errore',
    'Log': 'Log',
    'Device Log': 'Log dispositivo',
    'Close': 'Chiudi',
    'Clear': 'Pulisci',
    '(empty)': '(vuoto)',
    'Session History': 'Storico sessioni',
    'No sessions recorded yet.': 'Nessuna sessione registrata.',
    'Start (ms)': 'Avvio (ms)',
    'Duration (s)': 'Durata (s)',
    'Energy (Wh)': 'Energia (Wh)',
    'Peak P (W)': 'Picco P (W)',
    'Peak I (A)': 'Picco I (A)',
    'Error / Warning': 'Errore / Avviso',
    'State: {state}': 'Stato: {state}',
    'Last error: {reason}': 'Ultimo errore: {reason}',
    'Last stop: {reason}': 'Ultimo stop: {reason}',
    'No errors logged yet.': 'Nessun errore registrato.',
    'No warnings logged yet.': 'Nessun avviso registrato.',
    'Ton {on} ms / Toff {off} ms': 'Ton {on} ms / Toff {off} ms',
    'AP': 'AP',
    'Calibrate NTC': 'Calibra NTC',
    'Calibration help': 'Guida calibrazione',
    'Command failed': 'Comando fallito',
    'Duty': 'Duty',
    'Floor PI (suggested)': 'PI pavimento (suggerito)',
    'Help': 'Aiuto',
    'Idle': 'In attesa',
    'Interval': 'Intervallo',
    'LT': 'LT',
    'Load': 'Carica',
    'Load History': 'Carica storico',
    'Max': 'Max',
    'Mode': 'Modalita',
    'Most recent': 'Piu recente',
    'NTC Calibration': 'Calibrazione NTC',
    'NTC calibrate': 'Calibra NTC',
    'ON/OFF': 'ON/OFF',
    'Pause': 'Pausa',
    'Persist & Reload': 'Salva e ricarica',
    'READY': 'PRONTO',
    'Reference temp (C, blank = heatsink)':
        'Temp riferimento (C, vuoto = dissipatore)',
    'Resume': 'Riprendi',
    'Samples': 'Campioni',
    'Saved history': 'Storico salvato',
    'Start': 'Avvia',
    'State': 'Stato',
    'Stop': 'Ferma',
    'Suggest from History': 'Suggerisci da storico',
    'Target temp (C)': 'Temp target (C)',
    'Temp': 'Temp',
    'Temp Model Calibration': 'Calibrazione modello temp',
    'Temp model + PI': 'Modello temp + PI',
    'Temperature vs Time': 'Temperatura vs tempo',
    'Thermal model': 'Modello termico',
    'Time': 'Tempo',
    'Wi-Fi': 'Wi-Fi',
    'Wi-Fi {bars}/4': 'Wi-Fi {bars}/4',
    'Wire': 'Filo',
    'Wire PI (suggested)': 'PI filo (suggerito)',
    'Wire test': 'Test filo',
  };

  String t(String key, [Map<String, String>? params]) {
    var value = isItalian ? (_it[key] ?? key) : key;
    if (params != null && params.isNotEmpty) {
      params.forEach((k, v) {
        value = value.replaceAll('{$k}', v);
      });
    }
    return value;
  }
}

extension AppLanguageX on BuildContext {
  AppLanguageController get language => AppLanguageScope.of(this);
  AppStrings get strings => AppStrings(language.locale);
}

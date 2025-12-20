import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter_localizations/flutter_localizations.dart';

import 'l10n/app_strings.dart';
import 'screens/splash_screen.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  final languageController = await AppLanguageController.load(
    fallbackLocale: WidgetsBinding.instance.platformDispatcher.locale,
  );
  runApp(PowerBoardApp(languageController: languageController));
}

class PowerBoardApp extends StatelessWidget {
  const PowerBoardApp({super.key, required this.languageController});

  final AppLanguageController languageController;

  @override
  Widget build(BuildContext context) {
    const background = Color(0xFF0B0F12);
    final scheme = const ColorScheme.dark().copyWith(
      primary: const Color(0xFF1FE1B0),
      onPrimary: const Color(0xFF081214),
      secondary: const Color(0xFF6BE2FF),
      onSecondary: const Color(0xFF081214),
      surface: const Color(0xFF131A1F),
      onSurface: const Color(0xFFE6EEF2),
      error: const Color(0xFFE85B4A),
      onError: const Color(0xFF081214),
    );

    return AppLanguageScope(
      controller: languageController,
      child: AnimatedBuilder(
        animation: languageController,
        builder: (context, _) {
          return MaterialApp(
            title: 'PowerBoard Admin',
            debugShowCheckedModeBanner: false,
            locale: languageController.locale,
            scrollBehavior: const AppScrollBehavior(),
            localizationsDelegates: const [
              GlobalMaterialLocalizations.delegate,
              GlobalWidgetsLocalizations.delegate,
              GlobalCupertinoLocalizations.delegate,
            ],
            supportedLocales: const [
              Locale('en'),
              Locale('it'),
            ],
            theme: ThemeData(
              colorScheme: scheme,
              scaffoldBackgroundColor: background,
              fontFamily: 'Inter',
              useMaterial3: true,
              appBarTheme: const AppBarTheme(
                backgroundColor: Color(0xFF131A1F),
                foregroundColor: Color(0xFFE6EEF2),
              ),
              filledButtonTheme: FilledButtonThemeData(
                style: FilledButton.styleFrom(
                  padding: const EdgeInsets.symmetric(horizontal: 22, vertical: 14),
                  textStyle: const TextStyle(
                    fontSize: 15,
                    fontWeight: FontWeight.w600,
                  ),
                ),
              ),
            ),
            home: const SplashScreen(),
          );
        },
      ),
    );
  }
}

class AppScrollBehavior extends MaterialScrollBehavior {
  const AppScrollBehavior();

  @override
  ScrollPhysics getScrollPhysics(BuildContext context) {
    return const BouncingScrollPhysics(parent: AlwaysScrollableScrollPhysics());
  }

  @override
  Set<PointerDeviceKind> get dragDevices => const {
        PointerDeviceKind.touch,
        PointerDeviceKind.mouse,
        PointerDeviceKind.trackpad,
        PointerDeviceKind.stylus,
        PointerDeviceKind.invertedStylus,
      };
}

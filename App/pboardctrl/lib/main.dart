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
    final scheme = const ColorScheme.light().copyWith(
      primary: Color(0xFFF28C28),
      onPrimary: Color(0xFFFFFFFF),
      secondary: Color(0xFFF6B26B),
      onSecondary: Color(0xFF3A1D00),
      tertiary: Color(0xFF4C8DAE),
      onTertiary: Color(0xFFFFFFFF),
      surface: Color(0xFFFFFBF7),
      surfaceContainerHighest: Color(0xFFF1E3D6),
      onSurface: Color(0xFF2C1B13),
      error: Color(0xFFD64541),
      onError: Color(0xFFFFFFFF),
      outline: Color(0xFFE2D3C5),
    );
    final baseTextTheme = ThemeData.light().textTheme.apply(
          bodyColor: scheme.onSurface,
          displayColor: scheme.onSurface,
        );
    final theme = ThemeData(
      useMaterial3: true,
      colorScheme: scheme,
      fontFamily: 'Inter',
      textTheme: baseTextTheme,
      scaffoldBackgroundColor: Colors.transparent,
      canvasColor: scheme.surface,
      appBarTheme: AppBarTheme(
        backgroundColor: scheme.surface.withAlpha(242),
        foregroundColor: scheme.onSurface,
        elevation: 0,
        scrolledUnderElevation: 0,
        surfaceTintColor: Colors.transparent,
      ),
      cardTheme: CardThemeData(
        elevation: 0,
        color: scheme.surface,
        margin: EdgeInsets.zero,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(18),
          side: BorderSide(color: scheme.outline.withAlpha(140)),
        ),
      ),
      dialogTheme: DialogThemeData(
        backgroundColor: scheme.surface,
        surfaceTintColor: Colors.transparent,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(18),
          side: BorderSide(color: scheme.outline.withAlpha(140)),
        ),
      ),
      dividerTheme: DividerThemeData(
        color: scheme.outline.withAlpha(115),
      ),
      filledButtonTheme: FilledButtonThemeData(
        style: FilledButton.styleFrom(
          backgroundColor: scheme.primary,
          foregroundColor: scheme.onPrimary,
          padding: const EdgeInsets.symmetric(horizontal: 22, vertical: 14),
          textStyle: const TextStyle(fontSize: 15, fontWeight: FontWeight.w700),
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(14),
          ),
        ).copyWith(
          overlayColor: WidgetStateProperty.resolveWith((states) {
            if (states.contains(WidgetState.pressed)) {
              return scheme.onPrimary.withAlpha(46);
            }
            if (states.contains(WidgetState.hovered)) {
              return scheme.onPrimary.withAlpha(26);
            }
            return null;
          }),
        ),
      ),
      outlinedButtonTheme: OutlinedButtonThemeData(
        style: OutlinedButton.styleFrom(
          foregroundColor: scheme.primary,
          padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 14),
          textStyle: const TextStyle(fontWeight: FontWeight.w700),
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(14)),
        ).copyWith(
          side: WidgetStateProperty.resolveWith((states) {
            final color = states.contains(WidgetState.hovered) ||
                    states.contains(WidgetState.pressed)
                ? scheme.primary.withAlpha(160)
                : scheme.outline;
            return BorderSide(color: color);
          }),
          backgroundColor: WidgetStateProperty.resolveWith((states) {
            if (states.contains(WidgetState.pressed)) {
              return scheme.primary.withAlpha(26);
            }
            if (states.contains(WidgetState.hovered)) {
              return scheme.primary.withAlpha(18);
            }
            return null;
          }),
          overlayColor: WidgetStateProperty.resolveWith((states) {
            if (states.contains(WidgetState.pressed)) {
              return scheme.primary.withAlpha(20);
            }
            return null;
          }),
        ),
      ),
      textButtonTheme: TextButtonThemeData(
        style: TextButton.styleFrom(
          foregroundColor: scheme.primary,
          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
          textStyle: const TextStyle(fontWeight: FontWeight.w700),
        ).copyWith(
          backgroundColor: WidgetStateProperty.resolveWith((states) {
            if (states.contains(WidgetState.pressed)) {
              return scheme.primary.withAlpha(24);
            }
            if (states.contains(WidgetState.hovered)) {
              return scheme.primary.withAlpha(16);
            }
            return null;
          }),
        ),
      ),
      iconButtonTheme: IconButtonThemeData(
        style: ButtonStyle(
          foregroundColor: WidgetStatePropertyAll(scheme.onSurface),
          overlayColor: WidgetStateProperty.resolveWith((states) {
            if (states.contains(WidgetState.pressed)) {
              return scheme.primary.withAlpha(22);
            }
            if (states.contains(WidgetState.hovered)) {
              return scheme.primary.withAlpha(14);
            }
            return null;
          }),
        ),
      ),
      inputDecorationTheme: InputDecorationTheme(
        filled: true,
        fillColor: scheme.surfaceContainerHighest.withAlpha(110),
        contentPadding: const EdgeInsets.symmetric(horizontal: 14, vertical: 14),
        border: OutlineInputBorder(borderRadius: BorderRadius.circular(14)),
        enabledBorder: OutlineInputBorder(
          borderRadius: BorderRadius.circular(14),
          borderSide: BorderSide(color: scheme.outline),
        ),
        focusedBorder: OutlineInputBorder(
          borderRadius: BorderRadius.circular(14),
          borderSide: BorderSide(color: scheme.primary, width: 2),
        ),
        errorBorder: OutlineInputBorder(
          borderRadius: BorderRadius.circular(14),
          borderSide: BorderSide(color: scheme.error),
        ),
        focusedErrorBorder: OutlineInputBorder(
          borderRadius: BorderRadius.circular(14),
          borderSide: BorderSide(color: scheme.error, width: 2),
        ),
      ),
      switchTheme: SwitchThemeData(
        trackOutlineColor: WidgetStatePropertyAll(scheme.outline),
      ),
      sliderTheme: SliderThemeData(
        activeTrackColor: scheme.primary,
        inactiveTrackColor: scheme.outline.withAlpha(160),
        thumbColor: scheme.primary,
        overlayColor: scheme.primary.withAlpha(18),
      ),
      tooltipTheme: TooltipThemeData(
        decoration: BoxDecoration(
          color: scheme.onSurface.withAlpha(235),
          borderRadius: BorderRadius.circular(10),
        ),
        textStyle: TextStyle(
          color: scheme.surface,
          fontWeight: FontWeight.w600,
        ),
      ),
      snackBarTheme: SnackBarThemeData(
        behavior: SnackBarBehavior.floating,
        backgroundColor: scheme.onSurface.withAlpha(235),
        contentTextStyle: TextStyle(color: scheme.surface),
        actionTextColor: scheme.primary,
      ),
      scrollbarTheme: ScrollbarThemeData(
        radius: const Radius.circular(12),
        thickness: const WidgetStatePropertyAll(10),
        thumbColor: WidgetStatePropertyAll(scheme.primary.withAlpha(110)),
        trackColor:
            WidgetStatePropertyAll(scheme.surfaceContainerHighest.withAlpha(90)),
      ),
      popupMenuTheme: PopupMenuThemeData(
        color: scheme.surface,
        surfaceTintColor: Colors.transparent,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(14),
          side: BorderSide(color: scheme.outline.withAlpha(140)),
        ),
      ),
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
            builder: (context, child) {
              return DecoratedBox(
                decoration: const BoxDecoration(
                  gradient: RadialGradient(
                    center: Alignment.topLeft,
                    radius: 1.6,
                    colors: [
                      Color(0xFFFFFFFF),
                      Color(0xFFFFE9D2),
                      Color(0xFFF7F2EC),
                    ],
                  ),
                ),
                child: child ?? const SizedBox.shrink(),
              );
            },
            localizationsDelegates: const [
              GlobalMaterialLocalizations.delegate,
              GlobalWidgetsLocalizations.delegate,
              GlobalCupertinoLocalizations.delegate,
            ],
            supportedLocales: const [
              Locale('en'),
              Locale('it'),
            ],
            theme: theme,
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
    final platform = Theme.of(context).platform;
    return switch (platform) {
      TargetPlatform.iOS || TargetPlatform.macOS =>
        const BouncingScrollPhysics(parent: AlwaysScrollableScrollPhysics()),
      _ => const ClampingScrollPhysics(parent: AlwaysScrollableScrollPhysics()),
    };
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

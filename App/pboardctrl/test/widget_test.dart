// This is a basic Flutter widget test.
//
// To perform an interaction with a widget in your test, use the WidgetTester
// utility in the flutter_test package. For example, you can send tap and scroll
// gestures. You can also use WidgetTester to find child widgets in the widget
// tree, read text, and verify that the values of widget properties are correct.

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:pboardctrl/l10n/app_strings.dart';
import 'package:pboardctrl/main.dart';
import 'package:pboardctrl/screens/splash_screen.dart';

void main() {
  testWidgets('App shows splash screen', (WidgetTester tester) async {
    final languageController = AppLanguageController(
      initialLocale: const Locale('en'),
    );
    await tester.pumpWidget(
      PowerBoardApp(languageController: languageController),
    );

    expect(find.byType(SplashScreen), findsOneWidget);
  });
}

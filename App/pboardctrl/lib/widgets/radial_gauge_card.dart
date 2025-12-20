import 'dart:math' as math;

import 'package:flutter/material.dart';

class RadialGaugeCard extends StatelessWidget {
  const RadialGaugeCard({
    super.key,
    required this.title,
    required this.valueText,
    required this.color,
    required this.progress,
    this.subtitle,
    this.muted = false,
  });

  final String title;
  final String valueText;
  final String? subtitle;
  final Color color;
  final double progress; // 0..1
  final bool muted;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final clamped = progress.isFinite ? progress.clamp(0.0, 1.0) : 0.0;
    final fg = muted ? theme.colorScheme.outline : color;

    return Card(
      elevation: 0,
      color: theme.colorScheme.surface,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(18),
        side: BorderSide(color: theme.colorScheme.onSurface.withAlpha(20)),
      ),
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            SizedBox(
              width: 112,
              height: 112,
              child: CustomPaint(
                painter: _GaugePainter(
                  progress: clamped,
                  color: fg,
                  bg: theme.colorScheme.onSurface.withAlpha(26),
                ),
                child: Center(
                  child: Text(
                    valueText,
                    textAlign: TextAlign.center,
                    style: theme.textTheme.titleLarge?.copyWith(
                      fontWeight: FontWeight.w600,
                      color: muted
                          ? theme.colorScheme.onSurface.withAlpha(153)
                          : theme.colorScheme.onSurface,
                    ),
                  ),
                ),
              ),
            ),
            const SizedBox(height: 12),
            Text(
              title,
              style: theme.textTheme.titleSmall?.copyWith(
                fontWeight: FontWeight.w600,
              ),
            ),
            if (subtitle != null) ...[
              const SizedBox(height: 4),
              Text(
                subtitle!,
                style: theme.textTheme.bodySmall?.copyWith(
                  color: theme.colorScheme.onSurface.withAlpha(153),
                ),
              ),
            ],
          ],
        ),
      ),
    );
  }
}

class _GaugePainter extends CustomPainter {
  _GaugePainter({
    required this.progress,
    required this.color,
    required this.bg,
  });

  final double progress;
  final Color color;
  final Color bg;

  @override
  void paint(Canvas canvas, Size size) {
    final center = Offset(size.width / 2, size.height / 2);
    final radius = math.min(size.width, size.height) / 2 - 10;
    final stroke = 10.0;

    final basePaint = Paint()
      ..color = bg
      ..style = PaintingStyle.stroke
      ..strokeWidth = stroke
      ..strokeCap = StrokeCap.round;

    final fgPaint = Paint()
      ..color = color
      ..style = PaintingStyle.stroke
      ..strokeWidth = stroke
      ..strokeCap = StrokeCap.round;

    canvas.drawCircle(center, radius, basePaint);

    final startAngle = -math.pi / 2;
    final sweep = (2 * math.pi) * progress;
    canvas.drawArc(
      Rect.fromCircle(center: center, radius: radius),
      startAngle,
      sweep,
      false,
      fgPaint,
    );
  }

  @override
  bool shouldRepaint(covariant _GaugePainter oldDelegate) {
    return oldDelegate.progress != progress ||
        oldDelegate.color != color ||
        oldDelegate.bg != bg;
  }
}


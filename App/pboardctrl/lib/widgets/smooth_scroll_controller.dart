import 'dart:async';
import 'dart:math' as math;

import 'package:flutter/widgets.dart';

class SmoothScrollController extends ScrollController {
  SmoothScrollController({
    super.initialScrollOffset,
    super.keepScrollOffset,
    super.debugLabel,
    this.scrollAnimationDuration = const Duration(milliseconds: 160),
    this.curve = Curves.easeOutCubic,
    this.scrollDeltaMultiplier = 0.85,
  });

  final Duration scrollAnimationDuration;
  final Curve curve;

  /// Scales the incoming scroll wheel delta so mouse wheel steps feel less
  /// "ticky" on desktop platforms.
  final double scrollDeltaMultiplier;

  @override
  ScrollPosition createScrollPosition(
    ScrollPhysics physics,
    ScrollContext context,
    ScrollPosition? oldPosition,
  ) {
    return _SmoothScrollPosition(
      physics: physics,
      context: context,
      initialPixels: initialScrollOffset,
      keepScrollOffset: keepScrollOffset,
      oldPosition: oldPosition,
      debugLabel: debugLabel,
      scrollAnimationDuration: scrollAnimationDuration,
      curve: curve,
      scrollDeltaMultiplier: scrollDeltaMultiplier,
    );
  }
}

class _SmoothScrollPosition extends ScrollPositionWithSingleContext {
  _SmoothScrollPosition({
    required super.physics,
    required super.context,
    required this.scrollAnimationDuration,
    required this.curve,
    required this.scrollDeltaMultiplier,
    super.initialPixels,
    super.keepScrollOffset,
    super.oldPosition,
    super.debugLabel,
  });

  final Duration scrollAnimationDuration;
  final Curve curve;
  final double scrollDeltaMultiplier;

  double? _pendingTargetPixels;

  @override
  void pointerScroll(double delta) {
    if (delta == 0.0) {
      _pendingTargetPixels = null;
      goBallistic(0.0);
      return;
    }

    final base = _pendingTargetPixels ?? pixels;
    final effectiveDelta = delta * scrollDeltaMultiplier;
    final targetPixels = math.min(
      math.max(base + effectiveDelta, minScrollExtent),
      maxScrollExtent,
    );

    if (targetPixels == pixels) {
      _pendingTargetPixels = null;
      goBallistic(0.0);
      return;
    }

    _pendingTargetPixels = targetPixels;
    unawaited(
      animateTo(
        targetPixels,
        duration: scrollAnimationDuration,
        curve: curve,
      ).whenComplete(() {
        if (_pendingTargetPixels == targetPixels) {
          _pendingTargetPixels = null;
        }
      }),
    );
  }
}


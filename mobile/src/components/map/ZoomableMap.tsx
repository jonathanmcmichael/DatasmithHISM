import React, {useCallback, useState} from 'react';
import {Image, LayoutChangeEvent, StyleSheet, View} from 'react-native';
import {
  Gesture,
  GestureDetector,
} from 'react-native-gesture-handler';
import Animated, {
  useAnimatedStyle,
  useSharedValue,
  withTiming,
} from 'react-native-reanimated';
import PinLayer from './PinLayer';
import {Layout} from '@/constants/layout';
import type {MapPin} from '@/types/site';
import type {RenderedImageDimensions} from '@/utils/pinPositioning';

interface Props {
  imageUri: string;
  pins: MapPin[];
  onPinPress: (pin: MapPin) => void;
}

export default function ZoomableMap({imageUri, pins, onPinPress}: Props) {
  const [containerSize, setContainerSize] = useState({width: 0, height: 0});
  const [imageDims, setImageDims] = useState<RenderedImageDimensions>({
    width: 0,
    height: 0,
  });

  const scale = useSharedValue(1);
  const savedScale = useSharedValue(1);
  const translateX = useSharedValue(0);
  const translateY = useSharedValue(0);
  const savedTranslateX = useSharedValue(0);
  const savedTranslateY = useSharedValue(0);

  function clampTranslate(
    tx: number,
    ty: number,
    currentScale: number,
  ): {tx: number; ty: number} {
    const maxX = (Math.max(currentScale - 1, 0) * imageDims.width) / 2;
    const maxY = (Math.max(currentScale - 1, 0) * imageDims.height) / 2;
    return {
      tx: Math.max(-maxX, Math.min(maxX, tx)),
      ty: Math.max(-maxY, Math.min(maxY, ty)),
    };
  }

  const pinchGesture = Gesture.Pinch()
    .onUpdate(e => {
      const newScale = Math.max(
        Layout.mapMinScale,
        Math.min(Layout.mapMaxScale, savedScale.value * e.scale),
      );
      scale.value = newScale;
    })
    .onEnd(() => {
      savedScale.value = scale.value;
      // Re-clamp translate after scale changes
      const clamped = clampTranslate(
        translateX.value,
        translateY.value,
        scale.value,
      );
      translateX.value = withTiming(clamped.tx, {duration: 150});
      translateY.value = withTiming(clamped.ty, {duration: 150});
      savedTranslateX.value = clamped.tx;
      savedTranslateY.value = clamped.ty;
    });

  const panGesture = Gesture.Pan()
    .minPointers(1)
    .maxPointers(2)
    .onUpdate(e => {
      const {tx, ty} = clampTranslate(
        savedTranslateX.value + e.translationX,
        savedTranslateY.value + e.translationY,
        scale.value,
      );
      translateX.value = tx;
      translateY.value = ty;
    })
    .onEnd(() => {
      savedTranslateX.value = translateX.value;
      savedTranslateY.value = translateY.value;
    });

  const composed = Gesture.Simultaneous(pinchGesture, panGesture);

  const animatedStyle = useAnimatedStyle(() => ({
    transform: [
      {translateX: translateX.value},
      {translateY: translateY.value},
      {scale: scale.value},
    ],
  }));

  const onContainerLayout = useCallback((e: LayoutChangeEvent) => {
    setContainerSize({
      width: e.nativeEvent.layout.width,
      height: e.nativeEvent.layout.height,
    });
  }, []);

  const onImageLayout = useCallback(
    (e: LayoutChangeEvent) => {
      setImageDims({
        width: e.nativeEvent.layout.width,
        height: e.nativeEvent.layout.height,
      });
    },
    [],
  );

  return (
    <View style={styles.container} onLayout={onContainerLayout}>
      <GestureDetector gesture={composed}>
        <Animated.View style={[styles.mapWrapper, animatedStyle]}>
          <Image
            source={{uri: imageUri}}
            style={[
              styles.image,
              containerSize.width > 0 && {width: containerSize.width},
            ]}
            resizeMode="contain"
            onLayout={onImageLayout}
          />
          {imageDims.width > 0 && (
            <PinLayer
              pins={pins}
              dimensions={imageDims}
              onPinPress={onPinPress}
            />
          )}
        </Animated.View>
      </GestureDetector>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#1a1a1a',
    overflow: 'hidden',
  },
  mapWrapper: {
    flex: 1,
    alignItems: 'center',
    justifyContent: 'center',
  },
  image: {
    aspectRatio: undefined,
    height: undefined,
  },
});

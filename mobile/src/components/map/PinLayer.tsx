import React from 'react';
import {StyleSheet, View} from 'react-native';
import MapPin from './MapPin';
import {computePinPosition} from '@/utils/pinPositioning';
import {PIN_CATEGORIES} from '@/constants/pinCategories';
import type {MapPin as MapPinType} from '@/types/site';
import type {RenderedImageDimensions} from '@/utils/pinPositioning';

interface Props {
  pins: MapPinType[];
  dimensions: RenderedImageDimensions;
  onPinPress: (pin: MapPinType) => void;
}

export default function PinLayer({pins, dimensions, onPinPress}: Props) {
  const sorted = [...pins].sort(
    (a, b) =>
      (PIN_CATEGORIES[a.category]?.priority ?? 6) -
      (PIN_CATEGORIES[b.category]?.priority ?? 6),
  );

  return (
    <View
      style={[styles.layer, {width: dimensions.width, height: dimensions.height}]}
      pointerEvents="box-none">
      {sorted.map(pin => {
        const {left, top} = computePinPosition(
          pin.xRatio,
          pin.yRatio,
          dimensions,
        );
        return (
          <MapPin
            key={pin.id}
            pin={pin}
            left={left}
            top={top}
            onPress={onPinPress}
          />
        );
      })}
    </View>
  );
}

const styles = StyleSheet.create({
  layer: {
    position: 'absolute',
    top: 0,
    left: 0,
  },
});

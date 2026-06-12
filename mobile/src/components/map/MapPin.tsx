import React from 'react';
import {StyleSheet, TouchableOpacity, View} from 'react-native';
import Icon from 'react-native-vector-icons/MaterialCommunityIcons';
import {PIN_CATEGORIES} from '@/constants/pinCategories';
import {Layout} from '@/constants/layout';
import type {MapPin as MapPinType} from '@/types/site';

interface Props {
  pin: MapPinType;
  left: number;
  top: number;
  onPress: (pin: MapPinType) => void;
}

export default function MapPin({pin, left, top, onPress}: Props) {
  const config = PIN_CATEGORIES[pin.category];

  return (
    <TouchableOpacity
      style={[styles.pin, {left, top}]}
      onPress={() => onPress(pin)}
      activeOpacity={0.8}
      hitSlop={{top: 8, bottom: 8, left: 8, right: 8}}>
      <View style={[styles.bubble, {backgroundColor: config.color}]}>
        <Icon name={config.iconName} size={18} color="#fff" />
      </View>
      <View style={[styles.tip, {borderTopColor: config.color}]} />
    </TouchableOpacity>
  );
}

const styles = StyleSheet.create({
  pin: {
    position: 'absolute',
    width: Layout.pinSize,
    height: Layout.pinSize,
    alignItems: 'center',
  },
  bubble: {
    width: 30,
    height: 30,
    borderRadius: 15,
    alignItems: 'center',
    justifyContent: 'center',
    elevation: 3,
    shadowColor: '#000',
    shadowOffset: {width: 0, height: 2},
    shadowOpacity: 0.3,
    shadowRadius: 3,
  },
  tip: {
    width: 0,
    height: 0,
    borderLeftWidth: 5,
    borderRightWidth: 5,
    borderTopWidth: 6,
    borderLeftColor: 'transparent',
    borderRightColor: 'transparent',
  },
});

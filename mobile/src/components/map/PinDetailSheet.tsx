import React, {useCallback, useMemo, useRef} from 'react';
import {Linking, StyleSheet, Text, TouchableOpacity, View} from 'react-native';
import BottomSheet, {BottomSheetView} from '@gorhom/bottom-sheet';
import Icon from 'react-native-vector-icons/MaterialCommunityIcons';
import {PIN_CATEGORIES} from '@/constants/pinCategories';
import {Colors} from '@/constants/colors';
import {formatPhone} from '@/utils/formatters';
import type {MapPin} from '@/types/site';

interface Props {
  pin: MapPin | null;
  onClose: () => void;
}

export default function PinDetailSheet({pin, onClose}: Props) {
  const sheetRef = useRef<BottomSheet>(null);
  const snapPoints = useMemo(() => ['30%', '50%'], []);

  const handleSheetChange = useCallback(
    (index: number) => {
      if (index === -1) {
        onClose();
      }
    },
    [onClose],
  );

  if (!pin) {
    return null;
  }

  const config = PIN_CATEGORIES[pin.category];

  return (
    <BottomSheet
      ref={sheetRef}
      index={0}
      snapPoints={snapPoints}
      enablePanDownToClose
      onChange={handleSheetChange}
      handleIndicatorStyle={styles.handle}
      backgroundStyle={styles.background}>
      <BottomSheetView style={styles.content}>
        <View style={styles.header}>
          <View style={[styles.iconBadge, {backgroundColor: config.color + '20'}]}>
            <Icon name={config.iconName} size={22} color={config.color} />
          </View>
          <View style={styles.headerText}>
            <Text style={styles.label}>{pin.label}</Text>
            <Text style={[styles.category, {color: config.color}]}>
              {config.label}
            </Text>
          </View>
          <TouchableOpacity onPress={onClose} style={styles.closeBtn}>
            <Icon name="close" size={20} color={Colors.textSecondary} />
          </TouchableOpacity>
        </View>

        {pin.description ? (
          <Text style={styles.description}>{pin.description}</Text>
        ) : null}

        {pin.phone ? (
          <TouchableOpacity
            style={styles.callButton}
            onPress={() => Linking.openURL(`tel:${pin.phone}`)}
            activeOpacity={0.8}>
            <Icon name="phone" size={18} color={Colors.white} />
            <Text style={styles.callText}>Call {formatPhone(pin.phone)}</Text>
          </TouchableOpacity>
        ) : null}
      </BottomSheetView>
    </BottomSheet>
  );
}

const styles = StyleSheet.create({
  handle: {backgroundColor: Colors.border},
  background: {backgroundColor: Colors.surface},
  content: {padding: 20},
  header: {flexDirection: 'row', alignItems: 'center', gap: 12, marginBottom: 12},
  iconBadge: {
    width: 44,
    height: 44,
    borderRadius: 12,
    alignItems: 'center',
    justifyContent: 'center',
  },
  headerText: {flex: 1},
  label: {fontSize: 17, fontWeight: '700', color: Colors.textPrimary},
  category: {fontSize: 13, fontWeight: '600', marginTop: 2},
  closeBtn: {padding: 4},
  description: {
    fontSize: 14,
    color: Colors.textSecondary,
    lineHeight: 20,
    marginBottom: 16,
  },
  callButton: {
    flexDirection: 'row',
    alignItems: 'center',
    backgroundColor: Colors.primary,
    borderRadius: 10,
    padding: 14,
    gap: 8,
    justifyContent: 'center',
  },
  callText: {color: Colors.white, fontWeight: '700', fontSize: 15},
});

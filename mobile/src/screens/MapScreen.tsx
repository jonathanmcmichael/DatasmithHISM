import React, {useState} from 'react';
import {ActivityIndicator, StyleSheet, Text, View} from 'react-native';
import {SafeAreaView} from 'react-native-safe-area-context';
import {GestureHandlerRootView} from 'react-native-gesture-handler';
import {useSite} from '@/context/SiteContext';
import {useMapImage} from '@/hooks/useMapImage';
import ZoomableMap from '@/components/map/ZoomableMap';
import PinDetailSheet from '@/components/map/PinDetailSheet';
import ErrorBanner from '@/components/common/ErrorBanner';
import {Colors} from '@/constants/colors';
import type {MapPin} from '@/types/site';

export default function MapScreen() {
  const {state} = useSite();
  const {isLoading, error} = useMapImage();
  const [selectedPin, setSelectedPin] = useState<MapPin | null>(null);

  const activePins = state.pins.filter(p => p.isActive);

  return (
    <SafeAreaView style={styles.safe} edges={['top']}>
      <View style={styles.header}>
        <Text style={styles.title}>Site Map</Text>
        <Text style={styles.subtitle}>{state.site?.name}</Text>
      </View>

      {isLoading && (
        <View style={styles.loading}>
          <ActivityIndicator size="large" color={Colors.primary} />
          <Text style={styles.loadingText}>Loading map…</Text>
        </View>
      )}

      {error && !isLoading && (
        <ErrorBanner message={error} />
      )}

      {state.mapImageUrl && !isLoading && (
        <GestureHandlerRootView style={styles.mapContainer}>
          <ZoomableMap
            imageUri={state.mapImageUrl}
            pins={activePins}
            onPinPress={setSelectedPin}
          />
          {selectedPin && (
            <PinDetailSheet
              pin={selectedPin}
              onClose={() => setSelectedPin(null)}
            />
          )}
        </GestureHandlerRootView>
      )}
    </SafeAreaView>
  );
}

const styles = StyleSheet.create({
  safe: {flex: 1, backgroundColor: Colors.background},
  header: {
    paddingHorizontal: 16,
    paddingTop: 8,
    paddingBottom: 12,
    backgroundColor: Colors.surface,
    borderBottomWidth: 1,
    borderBottomColor: Colors.border,
  },
  title: {fontSize: 22, fontWeight: '700', color: Colors.textPrimary},
  subtitle: {fontSize: 13, color: Colors.textSecondary, marginTop: 2},
  loading: {
    flex: 1,
    alignItems: 'center',
    justifyContent: 'center',
    gap: 12,
  },
  loadingText: {color: Colors.textSecondary, fontSize: 15},
  mapContainer: {flex: 1},
});

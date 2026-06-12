import React from 'react';
import {StyleSheet, Text, TouchableOpacity, View} from 'react-native';
import Icon from 'react-native-vector-icons/MaterialCommunityIcons';
import {Colors} from '@/constants/colors';

interface Props {
  message: string;
  onRetry?: () => void;
}

export default function ErrorBanner({message, onRetry}: Props) {
  return (
    <View style={styles.container}>
      <Icon name="alert-circle" size={18} color={Colors.white} />
      <Text style={styles.text}>{message}</Text>
      {onRetry && (
        <TouchableOpacity onPress={onRetry} style={styles.retry}>
          <Text style={styles.retryText}>Retry</Text>
        </TouchableOpacity>
      )}
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flexDirection: 'row',
    alignItems: 'center',
    backgroundColor: Colors.danger,
    borderRadius: 8,
    padding: 12,
    margin: 16,
    gap: 8,
  },
  text: {
    flex: 1,
    color: Colors.white,
    fontSize: 14,
  },
  retry: {
    paddingHorizontal: 8,
  },
  retryText: {
    color: Colors.white,
    fontWeight: '600',
  },
});

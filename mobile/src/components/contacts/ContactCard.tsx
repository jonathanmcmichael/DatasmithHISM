import React from 'react';
import {
  Linking,
  StyleSheet,
  Text,
  TouchableOpacity,
  View,
} from 'react-native';
import Icon from 'react-native-vector-icons/MaterialCommunityIcons';
import {Colors} from '@/constants/colors';
import {formatPhone} from '@/utils/formatters';
import type {Contact} from '@/types/site';

interface Props {
  contact: Contact;
}

export default function ContactCard({contact}: Props) {
  const initials = contact.name
    .split(' ')
    .map(w => w[0])
    .slice(0, 2)
    .join('')
    .toUpperCase();

  return (
    <View style={styles.card}>
      <View style={styles.avatar}>
        <Text style={styles.initials}>{initials}</Text>
      </View>
      <View style={styles.info}>
        <Text style={styles.name}>{contact.name}</Text>
        <Text style={styles.title}>{contact.title}</Text>
      </View>
      <TouchableOpacity
        style={styles.phoneBtn}
        onPress={() => Linking.openURL(`tel:${contact.phone}`)}
        activeOpacity={0.7}>
        <Icon name="phone" size={16} color={Colors.primary} />
        <Text style={styles.phone}>{formatPhone(contact.phone)}</Text>
      </TouchableOpacity>
    </View>
  );
}

const styles = StyleSheet.create({
  card: {
    flexDirection: 'row',
    alignItems: 'center',
    backgroundColor: Colors.surface,
    borderRadius: 12,
    padding: 14,
    marginBottom: 10,
    gap: 12,
    elevation: 1,
    shadowColor: Colors.black,
    shadowOffset: {width: 0, height: 1},
    shadowOpacity: 0.06,
    shadowRadius: 3,
  },
  avatar: {
    width: 44,
    height: 44,
    borderRadius: 22,
    backgroundColor: Colors.primary,
    alignItems: 'center',
    justifyContent: 'center',
  },
  initials: {color: Colors.white, fontWeight: '700', fontSize: 15},
  info: {flex: 1},
  name: {fontSize: 15, fontWeight: '600', color: Colors.textPrimary},
  title: {fontSize: 13, color: Colors.textSecondary, marginTop: 2},
  phoneBtn: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 4,
    backgroundColor: Colors.primary + '15',
    borderRadius: 8,
    paddingHorizontal: 10,
    paddingVertical: 6,
  },
  phone: {color: Colors.primary, fontSize: 13, fontWeight: '600'},
});

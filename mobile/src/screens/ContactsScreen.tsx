import React from 'react';
import {FlatList, StyleSheet, Text, View} from 'react-native';
import {SafeAreaView} from 'react-native-safe-area-context';
import Icon from 'react-native-vector-icons/MaterialCommunityIcons';
import {useSite} from '@/context/SiteContext';
import ContactCard from '@/components/contacts/ContactCard';
import {Colors} from '@/constants/colors';

export default function ContactsScreen() {
  const {state} = useSite();
  const contacts = state.contacts.filter(c => c.isActive);

  return (
    <SafeAreaView style={styles.safe}>
      <View style={styles.header}>
        <Text style={styles.title}>Contacts</Text>
        <Text style={styles.subtitle}>{state.site?.name}</Text>
      </View>
      <FlatList
        data={contacts}
        keyExtractor={item => item.id}
        renderItem={({item}) => <ContactCard contact={item} />}
        contentContainerStyle={styles.list}
        ListEmptyComponent={
          <View style={styles.empty}>
            <Icon name="contacts-outline" size={48} color={Colors.textDisabled} />
            <Text style={styles.emptyText}>No contacts added yet</Text>
          </View>
        }
      />
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
  list: {padding: 16, paddingBottom: 32},
  empty: {alignItems: 'center', marginTop: 80, gap: 12},
  emptyText: {fontSize: 15, color: Colors.textSecondary},
});

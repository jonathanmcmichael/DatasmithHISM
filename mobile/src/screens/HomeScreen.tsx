import React from 'react';
import {
  Linking,
  ScrollView,
  StyleSheet,
  Text,
  TouchableOpacity,
  View,
} from 'react-native';
import {SafeAreaView} from 'react-native-safe-area-context';
import {useNavigation} from '@react-navigation/native';
import Icon from 'react-native-vector-icons/MaterialCommunityIcons';
import {useSite} from '@/context/SiteContext';
import QuickLinkCard from '@/components/home/QuickLinkCard';
import {Colors} from '@/constants/colors';

export default function HomeScreen() {
  const {state, dispatch} = useSite();
  const navigation = useNavigation<any>();
  const {site} = state;

  if (!site) {
    return null;
  }

  function callEmergency() {
    Linking.openURL(`tel:${site!.emergencyPhone}`);
  }

  function changeSite() {
    dispatch({type: 'CLEAR_SITE'});
    navigation.reset({index: 0, routes: [{name: 'CodeEntry'}]});
  }

  return (
    <SafeAreaView style={styles.safe}>
      <ScrollView contentContainerStyle={styles.content}>
        {/* Header */}
        <View style={styles.header}>
          <View style={styles.siteIcon}>
            <Icon name="hard-hat" size={32} color={Colors.primary} />
          </View>
          <View style={styles.siteInfo}>
            <Text style={styles.siteName}>{site.name}</Text>
            <Text style={styles.siteAddress}>{site.address}</Text>
          </View>
        </View>

        {/* Emergency */}
        <TouchableOpacity
          style={styles.emergency}
          onPress={callEmergency}
          activeOpacity={0.8}>
          <Icon name="phone-alert" size={20} color={Colors.white} />
          <Text style={styles.emergencyText}>Emergency: {site.emergencyPhone}</Text>
        </TouchableOpacity>

        {/* Quick links */}
        <Text style={styles.sectionLabel}>Site Resources</Text>
        <QuickLinkCard
          icon="map"
          label="Site Map"
          description="Find entrances, restrooms, and key areas"
          color={Colors.primary}
          onPress={() => navigation.navigate('Map')}
        />
        <QuickLinkCard
          icon="contacts"
          label="Contacts"
          description="Who to call on this job site"
          color="#059669"
          onPress={() => navigation.navigate('Contacts')}
        />
        <QuickLinkCard
          icon="file-document-multiple"
          label="Site Articles"
          description="Safety rules, logistics, and site info"
          color="#7C3AED"
          onPress={() => navigation.navigate('Articles')}
        />

        {/* Change site */}
        <TouchableOpacity style={styles.changeSite} onPress={changeSite}>
          <Icon name="swap-horizontal" size={16} color={Colors.textSecondary} />
          <Text style={styles.changeSiteText}>Change Site</Text>
        </TouchableOpacity>
      </ScrollView>
    </SafeAreaView>
  );
}

const styles = StyleSheet.create({
  safe: {flex: 1, backgroundColor: Colors.background},
  content: {padding: 16, paddingBottom: 32},
  header: {
    flexDirection: 'row',
    alignItems: 'center',
    backgroundColor: Colors.surface,
    borderRadius: 16,
    padding: 16,
    marginBottom: 12,
    gap: 12,
    elevation: 1,
    shadowColor: Colors.black,
    shadowOffset: {width: 0, height: 1},
    shadowOpacity: 0.08,
    shadowRadius: 4,
  },
  siteIcon: {
    width: 56,
    height: 56,
    borderRadius: 12,
    backgroundColor: Colors.primary + '15',
    alignItems: 'center',
    justifyContent: 'center',
  },
  siteInfo: {flex: 1},
  siteName: {fontSize: 17, fontWeight: '700', color: Colors.textPrimary},
  siteAddress: {fontSize: 13, color: Colors.textSecondary, marginTop: 4},
  emergency: {
    flexDirection: 'row',
    alignItems: 'center',
    backgroundColor: Colors.danger,
    borderRadius: 12,
    padding: 14,
    marginBottom: 20,
    gap: 10,
  },
  emergencyText: {color: Colors.white, fontWeight: '700', fontSize: 15},
  sectionLabel: {
    fontSize: 13,
    fontWeight: '600',
    color: Colors.textSecondary,
    textTransform: 'uppercase',
    letterSpacing: 1,
    marginBottom: 12,
  },
  changeSite: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'center',
    gap: 6,
    marginTop: 24,
    paddingVertical: 12,
  },
  changeSiteText: {color: Colors.textSecondary, fontSize: 14},
});

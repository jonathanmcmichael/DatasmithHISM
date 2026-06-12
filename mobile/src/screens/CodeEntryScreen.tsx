import React, {useEffect, useState} from 'react';
import {
  ActivityIndicator,
  Image,
  KeyboardAvoidingView,
  Platform,
  StyleSheet,
  Text,
  TextInput,
  TouchableOpacity,
  View,
} from 'react-native';
import {SafeAreaView} from 'react-native-safe-area-context';
import {useSite} from '@/context/SiteContext';
import {
  getLastSiteCode,
  loadSiteData,
  lookupSiteByCode,
  saveLastSiteCode,
} from '@/services/siteService';
import {Colors} from '@/constants/colors';
import type {CodeEntryScreenProps} from '@/types/navigation';

export default function CodeEntryScreen({navigation}: CodeEntryScreenProps) {
  const {dispatch} = useSite();
  const [code, setCode] = useState('');
  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    getLastSiteCode().then(saved => {
      if (saved) {
        setCode(saved);
      }
    });
  }, []);

  async function handleSubmit() {
    if (!code.trim()) {
      setError('Please enter a site code.');
      return;
    }
    setIsLoading(true);
    setError(null);
    dispatch({type: 'LOADING'});

    const lookupResult = await lookupSiteByCode(code);
    if (!lookupResult.ok) {
      setError(lookupResult.error);
      dispatch({type: 'ERROR', payload: lookupResult.error});
      setIsLoading(false);
      return;
    }

    const dataResult = await loadSiteData(lookupResult.data.id);
    if (!dataResult.ok) {
      setError(dataResult.error);
      dispatch({type: 'ERROR', payload: dataResult.error});
      setIsLoading(false);
      return;
    }

    await saveLastSiteCode(code);
    dispatch({
      type: 'SITE_LOADED',
      payload: {
        ...dataResult.data,
        mapImageUrl: null,
      },
    });
    navigation.navigate('SiteNavigator', {siteId: lookupResult.data.id});
    setIsLoading(false);
  }

  return (
    <SafeAreaView style={styles.safe}>
      <KeyboardAvoidingView
        style={styles.container}
        behavior={Platform.OS === 'ios' ? 'padding' : 'height'}>
        <View style={styles.inner}>
          <View style={styles.logoContainer}>
            <View style={styles.logoPlaceholder}>
              <Text style={styles.logoText}>SITE</Text>
              <Text style={styles.logoSub}>WAYFINDING</Text>
            </View>
          </View>

          <Text style={styles.title}>Enter Site Code</Text>
          <Text style={styles.subtitle}>
            Your site supervisor will provide your access code.
          </Text>

          <TextInput
            style={[styles.input, error ? styles.inputError : null]}
            value={code}
            onChangeText={t => {
              setCode(t.toUpperCase());
              setError(null);
            }}
            placeholder="e.g. TOWER-7"
            placeholderTextColor={Colors.textDisabled}
            autoCapitalize="characters"
            autoCorrect={false}
            maxLength={20}
            onSubmitEditing={handleSubmit}
            returnKeyType="go"
            editable={!isLoading}
          />

          {error && <Text style={styles.errorText}>{error}</Text>}

          <TouchableOpacity
            style={[styles.button, isLoading && styles.buttonDisabled]}
            onPress={handleSubmit}
            disabled={isLoading}
            activeOpacity={0.8}>
            {isLoading ? (
              <ActivityIndicator color={Colors.white} />
            ) : (
              <Text style={styles.buttonText}>Access Site</Text>
            )}
          </TouchableOpacity>
        </View>
      </KeyboardAvoidingView>
    </SafeAreaView>
  );
}

const styles = StyleSheet.create({
  safe: {flex: 1, backgroundColor: Colors.background},
  container: {flex: 1},
  inner: {
    flex: 1,
    alignItems: 'center',
    justifyContent: 'center',
    paddingHorizontal: 32,
  },
  logoContainer: {marginBottom: 40},
  logoPlaceholder: {
    width: 120,
    height: 120,
    borderRadius: 24,
    backgroundColor: Colors.primary,
    alignItems: 'center',
    justifyContent: 'center',
  },
  logoText: {
    color: Colors.white,
    fontSize: 24,
    fontWeight: '800',
    letterSpacing: 2,
  },
  logoSub: {
    color: Colors.white,
    fontSize: 10,
    fontWeight: '600',
    letterSpacing: 1,
    marginTop: 2,
  },
  title: {
    fontSize: 26,
    fontWeight: '700',
    color: Colors.textPrimary,
    marginBottom: 8,
  },
  subtitle: {
    fontSize: 14,
    color: Colors.textSecondary,
    textAlign: 'center',
    marginBottom: 32,
  },
  input: {
    width: '100%',
    height: 52,
    borderWidth: 1.5,
    borderColor: Colors.border,
    borderRadius: 10,
    paddingHorizontal: 16,
    fontSize: 18,
    fontWeight: '600',
    color: Colors.textPrimary,
    backgroundColor: Colors.surface,
    letterSpacing: 2,
    marginBottom: 8,
  },
  inputError: {borderColor: Colors.danger},
  errorText: {
    color: Colors.danger,
    fontSize: 13,
    alignSelf: 'flex-start',
    marginBottom: 16,
  },
  button: {
    width: '100%',
    height: 52,
    backgroundColor: Colors.primary,
    borderRadius: 10,
    alignItems: 'center',
    justifyContent: 'center',
    marginTop: 8,
  },
  buttonDisabled: {backgroundColor: Colors.textDisabled},
  buttonText: {color: Colors.white, fontSize: 16, fontWeight: '700'},
});

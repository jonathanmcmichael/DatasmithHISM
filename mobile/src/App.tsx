import React from 'react';
import {NavigationContainer} from '@react-navigation/native';
import {GestureHandlerRootView} from 'react-native-gesture-handler';
import {SafeAreaProvider} from 'react-native-safe-area-context';
import {SiteProvider} from '@/context/SiteContext';
import RootNavigator from '@/navigation/RootNavigator';

export default function App() {
  return (
    <GestureHandlerRootView style={{flex: 1}}>
      <SafeAreaProvider>
        <SiteProvider>
          <NavigationContainer>
            <RootNavigator />
          </NavigationContainer>
        </SiteProvider>
      </SafeAreaProvider>
    </GestureHandlerRootView>
  );
}

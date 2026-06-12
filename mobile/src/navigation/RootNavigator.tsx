import React from 'react';
import {createNativeStackNavigator} from '@react-navigation/native-stack';
import CodeEntryScreen from '@/screens/CodeEntryScreen';
import SiteTabNavigator from './SiteTabNavigator';
import type {RootStackParamList} from '@/types/navigation';

const Stack = createNativeStackNavigator<RootStackParamList>();

export default function RootNavigator() {
  return (
    <Stack.Navigator screenOptions={{headerShown: false}}>
      <Stack.Screen name="CodeEntry" component={CodeEntryScreen} />
      <Stack.Screen
        name="SiteNavigator"
        component={SiteTabNavigator}
        options={{gestureEnabled: false}}
      />
    </Stack.Navigator>
  );
}

import React from 'react';
import {createBottomTabNavigator} from '@react-navigation/bottom-tabs';
import Icon from 'react-native-vector-icons/MaterialCommunityIcons';
import {createNativeStackNavigator} from '@react-navigation/native-stack';

import HomeScreen from '@/screens/HomeScreen';
import MapScreen from '@/screens/MapScreen';
import ContactsScreen from '@/screens/ContactsScreen';
import ArticlesScreen from '@/screens/ArticlesScreen';
import ArticleDetailScreen from '@/screens/ArticleDetailScreen';
import {Colors} from '@/constants/colors';
import type {SiteTabParamList, SiteStackParamList} from '@/types/navigation';

const Tab = createBottomTabNavigator<SiteTabParamList>();
const SiteStack = createNativeStackNavigator<SiteStackParamList>();

function SiteTabs() {
  return (
    <Tab.Navigator
      screenOptions={({route}) => ({
        headerShown: false,
        tabBarActiveTintColor: Colors.primary,
        tabBarInactiveTintColor: Colors.textSecondary,
        tabBarStyle: {backgroundColor: Colors.surface},
        tabBarIcon: ({color, size}) => {
          const icons: Record<string, string> = {
            Home: 'home',
            Map: 'map',
            Contacts: 'contacts',
            Articles: 'file-document-multiple',
          };
          return (
            <Icon name={icons[route.name] ?? 'circle'} size={size} color={color} />
          );
        },
      })}>
      <Tab.Screen name="Home" component={HomeScreen} />
      <Tab.Screen name="Map" component={MapScreen} />
      <Tab.Screen name="Contacts" component={ContactsScreen} />
      <Tab.Screen name="Articles" component={ArticlesScreen} />
    </Tab.Navigator>
  );
}

export default function SiteTabNavigator() {
  return (
    <SiteStack.Navigator>
      <SiteStack.Screen
        name="SiteTabs"
        component={SiteTabs}
        options={{headerShown: false}}
      />
      <SiteStack.Screen
        name="ArticleDetail"
        component={ArticleDetailScreen}
        options={{title: 'Article', headerBackTitle: 'Back'}}
      />
    </SiteStack.Navigator>
  );
}

import React, {useMemo} from 'react';
import {SectionList, StyleSheet, Text, View} from 'react-native';
import {SafeAreaView} from 'react-native-safe-area-context';
import {useNavigation} from '@react-navigation/native';
import type {NativeStackNavigationProp} from '@react-navigation/native-stack';
import Icon from 'react-native-vector-icons/MaterialCommunityIcons';
import {useSite} from '@/context/SiteContext';
import ArticleListItem from '@/components/articles/ArticleListItem';
import {Colors} from '@/constants/colors';
import type {SiteStackParamList} from '@/types/navigation';

export default function ArticlesScreen() {
  const {state} = useSite();
  const navigation =
    useNavigation<NativeStackNavigationProp<SiteStackParamList>>();

  const sections = useMemo(() => {
    const active = state.articles.filter(a => a.isActive);
    const grouped: Record<string, typeof active> = {};
    for (const article of active) {
      if (!grouped[article.category]) {
        grouped[article.category] = [];
      }
      grouped[article.category].push(article);
    }
    return Object.entries(grouped).map(([title, data]) => ({title, data}));
  }, [state.articles]);

  return (
    <SafeAreaView style={styles.safe}>
      <View style={styles.header}>
        <Text style={styles.title}>Articles</Text>
        <Text style={styles.subtitle}>{state.site?.name}</Text>
      </View>
      <SectionList
        sections={sections}
        keyExtractor={item => item.id}
        renderItem={({item}) => (
          <ArticleListItem
            article={item}
            onPress={() =>
              navigation.navigate('ArticleDetail', {articleId: item.id})
            }
          />
        )}
        renderSectionHeader={({section: {title}}) => (
          <Text style={styles.sectionHeader}>{title}</Text>
        )}
        contentContainerStyle={styles.list}
        ListEmptyComponent={
          <View style={styles.empty}>
            <Icon
              name="file-document-outline"
              size={48}
              color={Colors.textDisabled}
            />
            <Text style={styles.emptyText}>No articles yet</Text>
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
  list: {paddingBottom: 32},
  sectionHeader: {
    fontSize: 12,
    fontWeight: '700',
    color: Colors.textSecondary,
    textTransform: 'uppercase',
    letterSpacing: 0.8,
    backgroundColor: Colors.background,
    paddingHorizontal: 16,
    paddingVertical: 8,
    borderBottomWidth: 1,
    borderBottomColor: Colors.border,
  },
  empty: {alignItems: 'center', marginTop: 80, gap: 12},
  emptyText: {fontSize: 15, color: Colors.textSecondary},
});

import React, {useMemo} from 'react';
import {ScrollView, StyleSheet, Text, View} from 'react-native';
import {SafeAreaView} from 'react-native-safe-area-context';
import Markdown from 'react-native-markdown-display';
import {useSite} from '@/context/SiteContext';
import {Colors} from '@/constants/colors';
import {formatDate} from '@/utils/formatters';
import type {ArticleDetailScreenProps} from '@/types/navigation';

export default function ArticleDetailScreen({
  route,
}: ArticleDetailScreenProps) {
  const {state} = useSite();
  const article = useMemo(
    () => state.articles.find(a => a.id === route.params.articleId),
    [state.articles, route.params.articleId],
  );

  if (!article) {
    return (
      <SafeAreaView style={styles.safe}>
        <Text style={styles.missing}>Article not found.</Text>
      </SafeAreaView>
    );
  }

  return (
    <SafeAreaView style={styles.safe} edges={['bottom']}>
      <ScrollView contentContainerStyle={styles.content}>
        <View style={styles.meta}>
          <Text style={styles.category}>{article.category}</Text>
          <Text style={styles.date}>{formatDate(article.publishedAt)}</Text>
        </View>
        <Text style={styles.title}>{article.title}</Text>
        <Markdown style={markdownStyles}>{article.body}</Markdown>
      </ScrollView>
    </SafeAreaView>
  );
}

const styles = StyleSheet.create({
  safe: {flex: 1, backgroundColor: Colors.surface},
  content: {padding: 20, paddingBottom: 48},
  meta: {flexDirection: 'row', alignItems: 'center', gap: 10, marginBottom: 10},
  category: {
    backgroundColor: Colors.primary + '20',
    color: Colors.primary,
    fontSize: 11,
    fontWeight: '700',
    textTransform: 'uppercase',
    letterSpacing: 0.8,
    paddingHorizontal: 8,
    paddingVertical: 3,
    borderRadius: 6,
  },
  date: {fontSize: 13, color: Colors.textSecondary},
  title: {
    fontSize: 24,
    fontWeight: '800',
    color: Colors.textPrimary,
    marginBottom: 20,
    lineHeight: 32,
  },
  missing: {padding: 24, color: Colors.textSecondary, fontSize: 16},
});

const markdownStyles = {
  body: {fontSize: 16, lineHeight: 26, color: Colors.textPrimary},
  heading1: {fontSize: 22, fontWeight: '700' as const, marginTop: 20},
  heading2: {fontSize: 18, fontWeight: '700' as const, marginTop: 16},
  bullet_list_icon: {color: Colors.primary},
  code_inline: {backgroundColor: Colors.background, borderRadius: 4},
};

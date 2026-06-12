import React from 'react';
import {StyleSheet, Text, TouchableOpacity, View} from 'react-native';
import Icon from 'react-native-vector-icons/MaterialCommunityIcons';
import {Colors} from '@/constants/colors';
import {formatDate} from '@/utils/formatters';
import type {Article} from '@/types/site';

interface Props {
  article: Article;
  onPress: () => void;
}

export default function ArticleListItem({article, onPress}: Props) {
  return (
    <TouchableOpacity style={styles.item} onPress={onPress} activeOpacity={0.7}>
      <View style={styles.content}>
        <Text style={styles.title} numberOfLines={2}>
          {article.title}
        </Text>
        <Text style={styles.date}>{formatDate(article.publishedAt)}</Text>
      </View>
      <Icon name="chevron-right" size={20} color={Colors.textSecondary} />
    </TouchableOpacity>
  );
}

const styles = StyleSheet.create({
  item: {
    flexDirection: 'row',
    alignItems: 'center',
    backgroundColor: Colors.surface,
    paddingHorizontal: 16,
    paddingVertical: 14,
    borderBottomWidth: 1,
    borderBottomColor: Colors.border,
    gap: 8,
  },
  content: {flex: 1},
  title: {fontSize: 15, fontWeight: '500', color: Colors.textPrimary},
  date: {fontSize: 12, color: Colors.textSecondary, marginTop: 3},
});

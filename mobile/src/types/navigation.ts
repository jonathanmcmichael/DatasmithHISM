import type {NativeStackScreenProps} from '@react-navigation/native-stack';
import type {BottomTabScreenProps} from '@react-navigation/bottom-tabs';
import type {CompositeScreenProps} from '@react-navigation/native';

export type RootStackParamList = {
  CodeEntry: undefined;
  SiteNavigator: {siteId: string};
};

export type SiteStackParamList = {
  SiteTabs: undefined;
  ArticleDetail: {articleId: string};
};

export type SiteTabParamList = {
  Home: undefined;
  Map: undefined;
  Contacts: undefined;
  Articles: undefined;
};

export type CodeEntryScreenProps = NativeStackScreenProps<
  RootStackParamList,
  'CodeEntry'
>;

export type SiteTabScreenProps<T extends keyof SiteTabParamList> =
  CompositeScreenProps<
    BottomTabScreenProps<SiteTabParamList, T>,
    NativeStackScreenProps<SiteStackParamList>
  >;

export type ArticleDetailScreenProps = NativeStackScreenProps<
  SiteStackParamList,
  'ArticleDetail'
>;

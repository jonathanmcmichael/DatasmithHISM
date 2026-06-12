import AsyncStorage from '@react-native-async-storage/async-storage';
import {firestore, storage} from './firebase';
import type {Site, MapPin, Contact, Article} from '@/types/site';

type Result<T> =
  | {ok: true; data: T}
  | {ok: false; error: string};

// eslint-disable-next-line @typescript-eslint/no-explicit-any
type DocData = Record<string, any>;

function toSite(id: string, data: DocData): Site {
  return {
    id,
    code: data.code,
    name: data.name,
    address: data.address,
    mapImagePath: data.mapImagePath,
    mapImageWidth: data.mapImageWidth,
    mapImageHeight: data.mapImageHeight,
    isActive: data.isActive,
    emergencyPhone: data.emergencyPhone,
    logoPath: data.logoPath ?? null,
    updatedAt: data.updatedAt?.toDate() ?? new Date(),
  };
}

function toPin(id: string, data: DocData): MapPin {
  return {
    id,
    label: data.label,
    category: data.category,
    xRatio: data.xRatio,
    yRatio: data.yRatio,
    description: data.description ?? null,
    phone: data.phone ?? null,
    isActive: data.isActive,
  };
}

function toContact(id: string, data: DocData): Contact {
  return {
    id,
    name: data.name,
    title: data.title,
    phone: data.phone,
    email: data.email ?? null,
    avatarPath: data.avatarPath ?? null,
    sortOrder: data.sortOrder ?? 0,
    isActive: data.isActive,
  };
}

function toArticle(id: string, data: DocData): Article {
  return {
    id,
    title: data.title,
    body: data.body,
    category: data.category ?? 'General',
    publishedAt: data.publishedAt?.toDate() ?? new Date(),
    isActive: data.isActive,
    sortOrder: data.sortOrder ?? 0,
  };
}

export async function lookupSiteByCode(
  code: string,
): Promise<Result<Site>> {
  try {
    const normalised = code.toUpperCase().trim();
    const codeDoc = await firestore()
      .collection('siteCodes')
      .doc(normalised)
      .get();

    if (!codeDoc.exists || !codeDoc.data()?.isActive) {
      return {ok: false, error: 'Invalid site code. Please try again.'};
    }

    const {siteId} = codeDoc.data() as {siteId: string};
    const siteDoc = await firestore().collection('sites').doc(siteId).get();

    if (!siteDoc.exists || !siteDoc.data()?.isActive) {
      return {ok: false, error: 'This site is no longer active.'};
    }

    return {ok: true, data: toSite(siteId, siteDoc.data()!)};
  } catch (e) {
    return {ok: false, error: 'Connection error. Check your signal and try again.'};
  }
}

export async function loadSiteData(siteId: string): Promise<
  Result<{
    site: Site;
    pins: MapPin[];
    contacts: Contact[];
    articles: Article[];
  }>
> {
  try {
    const [siteDoc, pinsSnap, contactsSnap, articlesSnap] = await Promise.all([
      firestore().collection('sites').doc(siteId).get(),
      firestore()
        .collection('sites')
        .doc(siteId)
        .collection('pins')
        .where('isActive', '==', true)
        .get(),
      firestore()
        .collection('sites')
        .doc(siteId)
        .collection('contacts')
        .where('isActive', '==', true)
        .orderBy('sortOrder')
        .get(),
      firestore()
        .collection('sites')
        .doc(siteId)
        .collection('articles')
        .where('isActive', '==', true)
        .orderBy('sortOrder')
        .get(),
    ]);

    const site = toSite(siteId, siteDoc.data()!);
    const pins = pinsSnap.docs.map(d => toPin(d.id, d.data()));
    const contacts = contactsSnap.docs.map(d => toContact(d.id, d.data()));
    const articles = articlesSnap.docs.map(d => toArticle(d.id, d.data()));

    return {ok: true, data: {site, pins, contacts, articles}};
  } catch (e) {
    return {ok: false, error: 'Failed to load site data. You may be offline.'};
  }
}

export async function getMapImageUrl(
  siteId: string,
  imagePath: string,
  updatedAt: Date,
): Promise<Result<string>> {
  const cacheKey = `mapUrl_${siteId}_${updatedAt.getTime()}`;
  try {
    const cached = await AsyncStorage.getItem(cacheKey);
    if (cached) {
      return {ok: true, data: cached};
    }
    const url = await storage().ref(imagePath).getDownloadURL();
    await AsyncStorage.setItem(cacheKey, url);
    return {ok: true, data: url};
  } catch (e) {
    return {ok: false, error: 'Could not load map image.'};
  }
}

export async function saveLastSiteCode(code: string): Promise<void> {
  await AsyncStorage.setItem('lastSiteCode', code.toUpperCase().trim());
}

export async function getLastSiteCode(): Promise<string | null> {
  return AsyncStorage.getItem('lastSiteCode');
}

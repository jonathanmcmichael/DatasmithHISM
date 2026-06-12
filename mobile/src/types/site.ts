export type PinCategory =
  | 'entrance'
  | 'restroom'
  | 'first_aid'
  | 'muster_point'
  | 'aed'
  | 'office_trailer'
  | 'laydown_area'
  | 'parking'
  | 'hazard'
  | 'general';

export interface MapPin {
  id: string;
  label: string;
  category: PinCategory;
  xRatio: number;
  yRatio: number;
  description: string | null;
  phone: string | null;
  isActive: boolean;
}

export interface Contact {
  id: string;
  name: string;
  title: string;
  phone: string;
  email: string | null;
  avatarPath: string | null;
  sortOrder: number;
  isActive: boolean;
}

export interface Article {
  id: string;
  title: string;
  body: string;
  category: string;
  publishedAt: Date;
  isActive: boolean;
  sortOrder: number;
}

export interface Site {
  id: string;
  code: string;
  name: string;
  address: string;
  mapImagePath: string;
  mapImageWidth: number;
  mapImageHeight: number;
  isActive: boolean;
  emergencyPhone: string;
  logoPath: string | null;
  updatedAt: Date;
}

export interface SiteState {
  site: Site | null;
  pins: MapPin[];
  contacts: Contact[];
  articles: Article[];
  mapImageUrl: string | null;
  isLoading: boolean;
  error: string | null;
}

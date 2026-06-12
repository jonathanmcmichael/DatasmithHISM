import type {PinCategory} from '@/types/site';

export interface PinCategoryConfig {
  category: PinCategory;
  label: string;
  color: string;
  iconName: string;
  priority: number;
}

export const PIN_CATEGORIES: Record<PinCategory, PinCategoryConfig> = {
  entrance: {
    category: 'entrance',
    label: 'Entrance / Gate',
    color: '#2563EB',
    iconName: 'gate',
    priority: 1,
  },
  restroom: {
    category: 'restroom',
    label: 'Restroom',
    color: '#0891B2',
    iconName: 'human-male-female',
    priority: 2,
  },
  first_aid: {
    category: 'first_aid',
    label: 'First Aid',
    color: '#DC2626',
    iconName: 'medical-bag',
    priority: 0,
  },
  muster_point: {
    category: 'muster_point',
    label: 'Muster Point',
    color: '#D97706',
    iconName: 'account-group',
    priority: 0,
  },
  aed: {
    category: 'aed',
    label: 'AED',
    color: '#DC2626',
    iconName: 'heart-pulse',
    priority: 0,
  },
  office_trailer: {
    category: 'office_trailer',
    label: 'Office Trailer',
    color: '#059669',
    iconName: 'office-building',
    priority: 3,
  },
  laydown_area: {
    category: 'laydown_area',
    label: 'Laydown Area',
    color: '#7C3AED',
    iconName: 'warehouse',
    priority: 4,
  },
  parking: {
    category: 'parking',
    label: 'Parking',
    color: '#6B7280',
    iconName: 'parking',
    priority: 5,
  },
  hazard: {
    category: 'hazard',
    label: 'Hazard',
    color: '#EF4444',
    iconName: 'alert',
    priority: 0,
  },
  general: {
    category: 'general',
    label: 'General',
    color: '#6B7280',
    iconName: 'map-marker',
    priority: 6,
  },
};

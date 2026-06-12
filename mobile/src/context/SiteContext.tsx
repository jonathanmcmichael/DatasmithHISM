import React, {
  createContext,
  useContext,
  useReducer,
  type ReactNode,
} from 'react';
import type {Site, MapPin, Contact, Article, SiteState} from '@/types/site';

type Action =
  | {type: 'LOADING'}
  | {
      type: 'SITE_LOADED';
      payload: {
        site: Site;
        pins: MapPin[];
        contacts: Contact[];
        articles: Article[];
        mapImageUrl: string | null;
      };
    }
  | {type: 'MAP_URL_LOADED'; payload: string}
  | {type: 'ERROR'; payload: string}
  | {type: 'CLEAR_SITE'};

const initialState: SiteState = {
  site: null,
  pins: [],
  contacts: [],
  articles: [],
  mapImageUrl: null,
  isLoading: false,
  error: null,
};

function reducer(state: SiteState, action: Action): SiteState {
  switch (action.type) {
    case 'LOADING':
      return {...state, isLoading: true, error: null};
    case 'SITE_LOADED':
      return {
        ...state,
        ...action.payload,
        isLoading: false,
        error: null,
      };
    case 'MAP_URL_LOADED':
      return {...state, mapImageUrl: action.payload};
    case 'ERROR':
      return {...state, isLoading: false, error: action.payload};
    case 'CLEAR_SITE':
      return initialState;
  }
}

interface SiteContextValue {
  state: SiteState;
  dispatch: React.Dispatch<Action>;
}

const SiteContext = createContext<SiteContextValue | null>(null);

export function SiteProvider({children}: {children: ReactNode}) {
  const [state, dispatch] = useReducer(reducer, initialState);
  return (
    <SiteContext.Provider value={{state, dispatch}}>
      {children}
    </SiteContext.Provider>
  );
}

export function useSite(): SiteContextValue {
  const ctx = useContext(SiteContext);
  if (!ctx) {
    throw new Error('useSite must be used inside SiteProvider');
  }
  return ctx;
}

import {useEffect, useState} from 'react';
import {getMapImageUrl} from '@/services/siteService';
import {useSite} from '@/context/SiteContext';

export function useMapImage() {
  const {state, dispatch} = useSite();
  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    if (!state.site || state.mapImageUrl) {
      return;
    }
    let cancelled = false;
    setIsLoading(true);
    getMapImageUrl(
      state.site.id,
      state.site.mapImagePath,
      state.site.updatedAt,
    ).then(result => {
      if (cancelled) {
        return;
      }
      setIsLoading(false);
      if (result.ok) {
        dispatch({type: 'MAP_URL_LOADED', payload: result.data});
      } else {
        setError(result.error);
      }
    });
    return () => {
      cancelled = true;
    };
  }, [state.site, state.mapImageUrl, dispatch]);

  return {isLoading, error};
}

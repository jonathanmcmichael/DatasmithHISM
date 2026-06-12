import firestore from '@react-native-firebase/firestore';
import storage from '@react-native-firebase/storage';

// Enable offline persistence for poor job-site connectivity
firestore().settings({
  persistence: true,
  cacheSizeBytes: firestore.CACHE_SIZE_UNLIMITED,
});

export {firestore, storage};

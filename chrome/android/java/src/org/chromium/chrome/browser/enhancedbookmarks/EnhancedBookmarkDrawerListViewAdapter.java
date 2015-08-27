// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.enhancedbookmarks;

import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.TextView;

import org.chromium.chrome.R;
import org.chromium.chrome.browser.enhancedbookmarks.EnhancedBookmarkManager.UIState;
import org.chromium.chrome.browser.offlinepages.OfflinePageBridge;
import org.chromium.components.bookmarks.BookmarkId;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * BaseAdapter for EnhancedBookmarkDrawerListView. It manages items to list there.
 */
class EnhancedBookmarkDrawerListViewAdapter extends BaseAdapter {
    static final int TYPE_FOLDER = 0;
    static final int TYPE_ALL_ITEMS = -1;
    static final int TYPE_DIVIDER = -2;
    static final int TYPE_FOLDERS_TITLE = -3;
    static final int TYPE_FILTER = -4;

    static final int VIEW_TYPE_ITEM = 0;
    static final int VIEW_TYPE_DIVIDER = 1;
    static final int VIEW_TYPE_TITLE = 2;

    private EnhancedBookmarkDelegate mDelegate;
    private List<Item> mTopSection = new ArrayList<Item>();
    private List<Item> mMiddleSection = new ArrayList<Item>();
    private List<Item> mBottomSection = new ArrayList<Item>();
    // array containing the order of sections
    private List<?>[] mSections = {mTopSection, mMiddleSection, mBottomSection};

    private BookmarkId mDesktopNodeId = null;
    private BookmarkId mMobileNodeId = null;
    private BookmarkId mOthersNodeId = null;
    private List<BookmarkId> mManagedAndPartnerFolderIds = null;

    /**
     * Represents each item in the list.
     */
    static class Item {
        final int mType;
        final BookmarkId mFolderId;
        final EnhancedBookmarkFilter mFilter;

        Item(int itemType) {
            mType = itemType;
            mFolderId = null;
            mFilter = null;
        }

        Item(BookmarkId folderId) {
            assert folderId != null;
            mType = TYPE_FOLDER;
            mFolderId = folderId;
            mFilter = null;
        }

        Item(EnhancedBookmarkFilter filter) {
            assert filter != null;
            mType = TYPE_FILTER;
            mFolderId = null;
            mFilter = filter;
        }

        @Override
        public int hashCode() {
            // hash function generated by Eclipse
            final int prime = 31;
            int result = 1;
            result = prime * result + ((mFolderId == null) ? 0 : mFolderId.hashCode());
            result = prime * result + mType;
            result = prime * result + ((mFilter == null) ? 0 : mFilter.ordinal());
            return result;
        }

        @Override
        public boolean equals(Object obj) {
            if (this == obj) return true;
            if (obj == null) return false;
            if (getClass() != obj.getClass()) return false;
            Item other = (Item) obj;
            if (mFolderId == null) {
                if (other.mFolderId != null) return false;
            } else if (!mFolderId.equals(other.mFolderId)) {
                return false;
            }
            if (mType != other.mType) {
                return false;
            }
            if (mFilter != other.mFilter) {
                return false;
            }
            return true;
        }
    }

    private void repopulateTopSection() {
        mTopSection.clear();
        mTopSection.add(new Item(TYPE_ALL_ITEMS));

        if (mDelegate.getModel().isFolderVisible(mMobileNodeId)) {
            mTopSection.add(new Item(mMobileNodeId));
        }
        if (mDelegate.getModel().isFolderVisible(mDesktopNodeId)) {
            mTopSection.add(new Item(mDesktopNodeId));
        }
        if (mDelegate.getModel().isFolderVisible(mOthersNodeId)) {
            mTopSection.add(new Item(mOthersNodeId));
        }
        if (OfflinePageBridge.isEnabled()) {
            mTopSection.add(new Item(EnhancedBookmarkFilter.OFFLINE_PAGES));
        }

        if (mManagedAndPartnerFolderIds != null) {
            for (BookmarkId id : mManagedAndPartnerFolderIds) {
                mTopSection.add(new Item(id));
            }
        }
    }

    void updateList() {
        mDesktopNodeId = mDelegate.getModel().getDesktopFolderId();
        mMobileNodeId = mDelegate.getModel().getMobileFolderId();
        mOthersNodeId = mDelegate.getModel().getOtherFolderId();
        mManagedAndPartnerFolderIds = mDelegate.getModel().getTopLevelFolderIDs(true, false);
        repopulateTopSection();

        setTopFolders(mDelegate.getModel().getTopLevelFolderIDs(false, true));
        notifyDataSetChanged();
    }

    /**
     * Sets folders to show.
     */
    void setTopFolders(List<BookmarkId> folders) {
        mMiddleSection.clear();

        if (folders.size() > 0) {
            // Add a divider and title to the top of the section.
            mMiddleSection.add(new Item(TYPE_DIVIDER));
            mMiddleSection.add(new Item(TYPE_FOLDERS_TITLE));
        }

        // Add the rest of the items.
        for (BookmarkId id : folders) {
            mMiddleSection.add(new Item(id));
        }
    }

    /**
     * Clear everything so that it doesn't have any entry.
     */
    void clear() {
        mTopSection.clear();
        mMiddleSection.clear();
        mBottomSection.clear();
    }

    void setEnhancedBookmarkUIDelegate(EnhancedBookmarkDelegate delegate) {
        mDelegate = delegate;
    }

    /**
     * Get the position in the list of a given item
     * @param item Item to search for
     * @return index of the item or -1 if not found
     */
    int positionOfItem(Item item) {
        int offset = 0;
        for (List<?> section : mSections) {
            int index = section.indexOf(item);
            if (index != -1) {
                return offset + index;
            }
            // If not found in current section, offset the potential result by
            // the section size.
            offset += section.size();
        }
        return -1;
    }

    /**
     * Get the position in the list of a given bookmark folder id
     * @param id Id of bookmark folder
     * @return index of bookmark folder or -1 if not found
     */
    int positionOfBookmarkId(BookmarkId id) {
        return positionOfItem(new Item(id));
    }

    /**
     * Get item position of the given mode.
     */
    int getItemPosition(int state, Object modeDetail) {
        if (state == UIState.STATE_ALL_BOOKMARKS) {
            return 0;
        } else if (state == UIState.STATE_FOLDER) {
            Set<BookmarkId> topLevelFolderParents = new HashSet<>();
            topLevelFolderParents.addAll(mDelegate.getModel().getTopLevelFolderParentIDs());
            topLevelFolderParents.add(mDesktopNodeId);
            topLevelFolderParents.add(mOthersNodeId);
            topLevelFolderParents.add(mMobileNodeId);

            // Find top folder id that contains |folder|.
            BookmarkId topFolderId = (BookmarkId) modeDetail;
            while (true) {
                BookmarkId parentId =
                        mDelegate.getModel().getBookmarkById(topFolderId).getParentId();
                if (topLevelFolderParents.contains(parentId)) {
                    break;
                }
                topFolderId = parentId;
            }
            return positionOfBookmarkId(topFolderId);
        } else if (state == UIState.STATE_FILTER) {
            EnhancedBookmarkFilter filter = (EnhancedBookmarkFilter) modeDetail;
            return positionOfItem(new Item(filter));
        }

        return -1;
    }

    // BaseAdapter implementations.

    @Override
    public boolean areAllItemsEnabled() {
        return false;
    }

    @Override
    public boolean isEnabled(int position) {
        Item item = (Item) getItem(position);
        return item.mType != TYPE_DIVIDER && item.mType != TYPE_FOLDERS_TITLE;
    }

    @Override
    public int getItemViewType(int position) {
        Item item = (Item) getItem(position);
        if (item.mType == TYPE_DIVIDER) {
            return VIEW_TYPE_DIVIDER;
        } else if (item.mType == TYPE_FOLDERS_TITLE) {
            return VIEW_TYPE_TITLE;
        } else {
            return VIEW_TYPE_ITEM;
        }
    }

    @Override
    public int getViewTypeCount() {
        return 3;
    }

    @Override
    public int getCount() {
        int sum = 0;
        for (List<?> section : mSections) {
            sum += section.size();
        }
        return sum;
    }

    @Override
    public long getItemId(int position) {
        return position;
    }

    @Override
    public Object getItem(int position) {
        if (position < 0) {
            return null;
        }
        for (List<?> section : mSections) {
            if (position < section.size()) {
                return section.get(position);
            }
            position -= section.size();
        }
        return null;
    }

    @Override
    public View getView(int position, View convertView, ViewGroup parent) {
        final Item item = (Item) getItem(position);

        // Inflate view if convertView is null.
        if (convertView == null) {
            final int itemViewType = getItemViewType(position);
            if (itemViewType == VIEW_TYPE_ITEM) {
                convertView = LayoutInflater.from(parent.getContext()).inflate(
                        R.layout.eb_drawer_item, parent, false);
            } else if (itemViewType == VIEW_TYPE_DIVIDER) {
                convertView = LayoutInflater.from(parent.getContext()).inflate(
                        R.layout.eb_divider, parent, false);
            } else if (itemViewType == VIEW_TYPE_TITLE) {
                convertView = LayoutInflater.from(parent.getContext()).inflate(
                        R.layout.eb_drawer_title, parent, false);
            } else {
                assert false : "Invalid item view type.";
            }
        }

        if (item.mType == TYPE_DIVIDER) return convertView;

        if (item.mType == TYPE_FOLDERS_TITLE) {
            String title = convertView.getContext().getResources().getString(
                    R.string.enhanced_bookmark_drawer_folders);
            ((TextView) convertView).setText(title);
            return convertView;
        }

        final EnhancedBookmarkDrawerListItemView listItemView =
                (EnhancedBookmarkDrawerListItemView) convertView;
        String title;
        int iconDrawableId;

        switch (item.mType) {
            case TYPE_ALL_ITEMS:
                title = listItemView.getContext().getResources()
                        .getString(R.string.enhanced_bookmark_drawer_all_items);
                iconDrawableId = R.drawable.btn_star;
                break;
            case TYPE_FOLDER:
                title = mDelegate.getModel().getBookmarkById(item.mFolderId).getTitle();
                if (mManagedAndPartnerFolderIds != null
                        && mManagedAndPartnerFolderIds.contains(item.mFolderId)) {
                    iconDrawableId = R.drawable.eb_managed;
                } else if (item.mFolderId.equals(mMobileNodeId)
                        || item.mFolderId.equals(mOthersNodeId)
                        || item.mFolderId.equals(mDesktopNodeId)) {
                    iconDrawableId = R.drawable.eb_folder;
                } else {
                    iconDrawableId = 0;
                }
                break;
            case TYPE_FILTER:
                assert item.mFilter == EnhancedBookmarkFilter.OFFLINE_PAGES;
                title = listItemView.getContext().getResources().getString(
                        R.string.enhanced_bookmark_drawer_filter_offline_pages);
                iconDrawableId = R.drawable.eb_filter_offline_pages;
                break;
            default:
                title = "";
                iconDrawableId = 0;
                assert false;
        }

        listItemView.setText(title);
        listItemView.setIcon(iconDrawableId);

        return convertView;
    }
}

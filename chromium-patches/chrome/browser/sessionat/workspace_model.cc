// Copyright 2024 Sessionat. All rights reserved.
// Workspace data model implementation.

#include "chrome/browser/sessionat/workspace_model.h"

#include "base/uuid.h"
#include "base/strings/string_number_conversions.h"

namespace sessionat {

namespace {

// Keys for WorkspaceItem serialization.
constexpr char kItemIdKey[] = "id";
constexpr char kItemUrlKey[] = "url";
constexpr char kItemTitleKey[] = "title";
constexpr char kItemFaviconUrlKey[] = "favicon_url";
constexpr char kItemFolderPathKey[] = "folder_path";
constexpr char kItemTagsKey[] = "tags";
constexpr char kItemCreatedAtKey[] = "created_at";
constexpr char kItemLastOpenedAtKey[] = "last_opened_at";

// Keys for Workspace serialization.
constexpr char kWorkspaceIdKey[] = "id";
constexpr char kWorkspaceNameKey[] = "name";
constexpr char kWorkspaceColorKey[] = "color";
constexpr char kWorkspaceIconKey[] = "icon";
constexpr char kWorkspaceDescriptionKey[] = "description";
constexpr char kWorkspaceTagsKey[] = "tags";
constexpr char kWorkspaceItemsKey[] = "items";
constexpr char kWorkspaceCreatedAtKey[] = "created_at";
constexpr char kWorkspaceModifiedAtKey[] = "modified_at";
constexpr char kWorkspaceLastOpenedAtKey[] = "last_opened_at";
constexpr char kWorkspaceIsActiveKey[] = "is_active";
constexpr char kWorkspaceIsPinnedKey[] = "is_pinned";
constexpr char kWorkspaceSortOrderKey[] = "sort_order";

std::string TimeToString(base::Time time) {
  return base::NumberToString(time.InMillisecondsFSinceUnixEpoch());
}

base::Time StringToTime(const std::string& str) {
  double ms;
  if (base::StringToDouble(str, &ms)) {
    return base::Time::FromMillisecondsSinceUnixEpoch(ms);
  }
  return base::Time();
}

}  // anonymous namespace

// WorkspaceItem implementation.
WorkspaceItem::WorkspaceItem() = default;
WorkspaceItem::WorkspaceItem(const WorkspaceItem&) = default;
WorkspaceItem& WorkspaceItem::operator=(const WorkspaceItem&) = default;
WorkspaceItem::WorkspaceItem(WorkspaceItem&&) = default;
WorkspaceItem& WorkspaceItem::operator=(WorkspaceItem&&) = default;
WorkspaceItem::~WorkspaceItem() = default;

// Workspace implementation.
Workspace::Workspace() = default;
Workspace::Workspace(const Workspace&) = default;
Workspace& Workspace::operator=(const Workspace&) = default;
Workspace::Workspace(Workspace&&) = default;
Workspace& Workspace::operator=(Workspace&&) = default;
Workspace::~Workspace() = default;

base::DictValue WorkspaceItem::ToDict() const {
  base::DictValue dict;
  dict.Set(kItemIdKey, id);
  dict.Set(kItemUrlKey, url.spec());
  dict.Set(kItemTitleKey, title);
  dict.Set(kItemFaviconUrlKey, favicon_url.spec());
  dict.Set(kItemFolderPathKey, folder_path);

  base::ListValue tags_list;
  for (const auto& tag : tags) {
    tags_list.Append(tag);
  }
  dict.Set(kItemTagsKey, std::move(tags_list));

  dict.Set(kItemCreatedAtKey, TimeToString(created_at));
  dict.Set(kItemLastOpenedAtKey, TimeToString(last_opened_at));

  return dict;
}

// static
std::optional<WorkspaceItem> WorkspaceItem::FromDict(
    const base::DictValue& dict) {
  WorkspaceItem item;

  const std::string* id = dict.FindString(kItemIdKey);
  if (!id) {
    return std::nullopt;
  }
  item.id = *id;

  const std::string* url_str = dict.FindString(kItemUrlKey);
  if (url_str) {
    item.url = GURL(*url_str);
  }

  const std::string* title = dict.FindString(kItemTitleKey);
  if (title) {
    item.title = *title;
  }

  const std::string* favicon_url = dict.FindString(kItemFaviconUrlKey);
  if (favicon_url) {
    item.favicon_url = GURL(*favicon_url);
  }

  const std::string* folder_path = dict.FindString(kItemFolderPathKey);
  if (folder_path) {
    item.folder_path = *folder_path;
  }

  const base::ListValue* tags_list = dict.FindList(kItemTagsKey);
  if (tags_list) {
    for (const auto& tag_value : *tags_list) {
      if (tag_value.is_string()) {
        item.tags.push_back(tag_value.GetString());
      }
    }
  }

  const std::string* created_at = dict.FindString(kItemCreatedAtKey);
  if (created_at) {
    item.created_at = StringToTime(*created_at);
  }

  const std::string* last_opened_at = dict.FindString(kItemLastOpenedAtKey);
  if (last_opened_at) {
    item.last_opened_at = StringToTime(*last_opened_at);
  }

  return item;
}

base::DictValue Workspace::ToDict() const {
  base::DictValue dict;
  dict.Set(kWorkspaceIdKey, id);
  dict.Set(kWorkspaceNameKey, name);
  dict.Set(kWorkspaceColorKey, color);
  dict.Set(kWorkspaceIconKey, icon);
  dict.Set(kWorkspaceDescriptionKey, description);

  base::ListValue tags_list;
  for (const auto& tag : tags) {
    tags_list.Append(tag);
  }
  dict.Set(kWorkspaceTagsKey, std::move(tags_list));

  base::ListValue items_list;
  for (const auto& item : items) {
    items_list.Append(item.ToDict());
  }
  dict.Set(kWorkspaceItemsKey, std::move(items_list));

  dict.Set(kWorkspaceCreatedAtKey, TimeToString(created_at));
  dict.Set(kWorkspaceModifiedAtKey, TimeToString(modified_at));
  dict.Set(kWorkspaceLastOpenedAtKey, TimeToString(last_opened_at));
  dict.Set(kWorkspaceIsActiveKey, is_active);
  dict.Set(kWorkspaceIsPinnedKey, is_pinned);
  dict.Set(kWorkspaceSortOrderKey, sort_order);

  return dict;
}

// static
std::optional<Workspace> Workspace::FromDict(const base::DictValue& dict) {
  Workspace workspace;

  const std::string* id = dict.FindString(kWorkspaceIdKey);
  if (!id) {
    return std::nullopt;
  }
  workspace.id = *id;

  const std::string* name = dict.FindString(kWorkspaceNameKey);
  if (name) {
    workspace.name = *name;
  }

  const std::string* color = dict.FindString(kWorkspaceColorKey);
  if (color) {
    workspace.color = *color;
  }

  const std::string* icon = dict.FindString(kWorkspaceIconKey);
  if (icon) {
    workspace.icon = *icon;
  }

  const std::string* description = dict.FindString(kWorkspaceDescriptionKey);
  if (description) {
    workspace.description = *description;
  }

  const base::ListValue* tags_list = dict.FindList(kWorkspaceTagsKey);
  if (tags_list) {
    for (const auto& tag_value : *tags_list) {
      if (tag_value.is_string()) {
        workspace.tags.push_back(tag_value.GetString());
      }
    }
  }

  const base::ListValue* items_list = dict.FindList(kWorkspaceItemsKey);
  if (items_list) {
    for (const auto& item_value : *items_list) {
      if (item_value.is_dict()) {
        auto item = WorkspaceItem::FromDict(item_value.GetDict());
        if (item) {
          workspace.items.push_back(std::move(*item));
        }
      }
    }
  }

  const std::string* created_at = dict.FindString(kWorkspaceCreatedAtKey);
  if (created_at) {
    workspace.created_at = StringToTime(*created_at);
  }

  const std::string* modified_at = dict.FindString(kWorkspaceModifiedAtKey);
  if (modified_at) {
    workspace.modified_at = StringToTime(*modified_at);
  }

  const std::string* last_opened_at =
      dict.FindString(kWorkspaceLastOpenedAtKey);
  if (last_opened_at) {
    workspace.last_opened_at = StringToTime(*last_opened_at);
  }

  workspace.is_active = dict.FindBool(kWorkspaceIsActiveKey).value_or(false);
  workspace.is_pinned = dict.FindBool(kWorkspaceIsPinnedKey).value_or(false);
  workspace.sort_order = dict.FindInt(kWorkspaceSortOrderKey).value_or(0);

  return workspace;
}

bool Workspace::ContainsUrl(const GURL& url) const {
  for (const auto& item : items) {
    if (item.url == url) {
      return true;
    }
  }
  return false;
}

const WorkspaceItem* Workspace::FindItemByUrl(const GURL& url) const {
  for (const auto& item : items) {
    if (item.url == url) {
      return &item;
    }
  }
  return nullptr;
}

}  // namespace sessionat

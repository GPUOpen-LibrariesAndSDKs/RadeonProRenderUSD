import hashlib
import json
import os
import re
import textwrap
from enum import Enum
from pathlib import Path
from typing import Any, Callable, Collection, Dict, Tuple, Union
from urllib.parse import parse_qsl, unquote, urlencode, urljoin, urlparse, urlunparse
from uuid import UUID

from requests import Response, Session
from requests.exceptions import HTTPError


class MaterialType(Enum):
    STATIC = "Static"
    PARAMETRIC = "Parametric"


class MatlibEndpoint(Enum):
    PREFIX = "api"
    AUTH = "auth"
    AUTH_LOGIN = "auth/login"
    AUTH_LOGOUT = "auth/logout"
    AUTH_REFRESH = "auth/token/refresh"
    MATERIALS = "materials"
    CATEGORIES = "categories"
    COLLECTIONS = "collections"
    TAGS = "tags"
    RENDERS = "renders"
    PACKAGES = "packages"


def expanded_raise_for_status(
    response: Response, on_status_callbacks: Dict[int, Callable] = None
):
    if not on_status_callbacks:
        on_status_callbacks = {}
    try:
        if callback := on_status_callbacks.get(response.status_code, None):
            callback()
        else:
            response.raise_for_status()
    except HTTPError as e:
        raise HTTPError(json.dumps(response.json(), indent=4) if response.text else e)


class MatlibSession(Session):
    def __init__(self, base: str, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.base = base
        self.recent = {}
        self.refresh_token = ""

    def __set_auth_token(self, access_token: str, refresh_token: str):
        self.headers.update({"Authorization": f"Bearer {access_token}"})
        self.refresh_token = refresh_token

    def _refresh_token(self, endpoint: str, on_action_success: Callable = None):
        if self.refresh_token:
            response = self.post(
                urljoin(
                    base=self.base, url=f"/{MatlibEndpoint.PREFIX.value}/{endpoint}/"
                ),
                json={"refresh_token": self.refresh_token},
            )
            expanded_raise_for_status(response)
            if on_action_success:
                on_action_success(response)
        else:
            raise Exception(
                textwrap.dedent(
                    """
						You didn't login yet, first call `client.session.login()` 
					"""
                )
            )

    def refresh_session(self):
        def on_refresh_success(response: Response):
            content = response.json()
            self.__set_auth_token(content["access_token"], content["refresh_token"])

        self._refresh_token(MatlibEndpoint.AUTH_REFRESH.value, on_refresh_success)

    def _login(self, password: str, email: str = "", username: str = ""):
        if not email and not username:
            raise Exception("You should provide email OR username AND password")
        response = self.post(
            url=urljoin(
                base=self.base,
                url=f"/{MatlibEndpoint.PREFIX.value}/{MatlibEndpoint.AUTH_LOGIN.value}/",
            ),
            json={"username": username, "email": email, "password": password},
        )
        expanded_raise_for_status(response)
        content = response.json()
        self.__set_auth_token(content["access_token"], content["refresh_token"])

    def _logout(self):
        self._refresh_token(MatlibEndpoint.AUTH_LOGOUT.value)

    @staticmethod
    def add_url_params(url: str, params: Dict):
        """Add GET params to provided URL being aware of existing.

        :param url: string of target URL
        :param params: dict containing requested params to be added
        :return: string with updated URL
        """
        parsed_url = urlparse(unquote(url))
        query_dict = dict(parse_qsl(parsed_url.query))
        # convert bool and dict to json strings
        query_dict = {
            k: json.dumps(v) if isinstance(v, (bool, dict)) else v
            for k, v in query_dict.items()
        }
        # collect value to dict if they are not None
        for k, v in params.items():
            if v:
                query_dict.update({k: v})
        parsed_url = parsed_url._replace(query=urlencode(query_dict, doseq=True))
        return urlunparse(parsed_url)

    @staticmethod
    def get_last_url_path(url: str):
        return urlparse(url).path.rsplit("/", 1)[-1]

    def clear_recent(self):
        self.recent = {}


class MatlibEntityClient:
    endpoint = None
    session = None
    __working_on_id = ""

    def __init__(
        self,
        session: MatlibSession,
        endpoint: MatlibEndpoint,
    ):
        self.session = session
        self.endpoint = endpoint

    @property
    def base_url(self):
        return urljoin(
            self.session.base, f"/{MatlibEndpoint.PREFIX.value}/{self.endpoint.value}/"
        )

    def delete(self, item_id: Union[str, UUID] = ""):
        response = self.session.delete(
            urljoin(self.base_url, f"{item_id if item_id else self.working_on}/")
        )
        expanded_raise_for_status(response, {401: self.session.refresh_session})
        self.stop_working_on()

    def _get_list(
        self,
        url: str = None,
        limit: int = None,
        offset: int = None,
        params: Dict = None,
    ):
        url = urljoin(base=self.base_url, url=url)
        url = self.session.add_url_params(url, {"limit": limit, "offset": offset})
        if params:
            url = self.session.add_url_params(url, params)
        response = self.session.get(url)
        expanded_raise_for_status(response, {401: self.session.refresh_session})
        return response.json()["results"]

    def _get_by_id(
        self, item_id: Union[str, UUID] = "", url: str = None, working_on: bool = False
    ):
        url = urljoin(base=self.base_url, url=url)
        url = urljoin(base=url, url=f"{item_id if item_id else self.working_on}/")
        response = self.session.get(url)
        expanded_raise_for_status(response, {401: self.session.refresh_session})
        content = response.json()
        if working_on:
            self.__working_on_id = content["id"]
        return content

    def _get(self, url: str = None):
        url = urljoin(base=self.base_url, url=url)
        response = self.session.get(url)
        expanded_raise_for_status(response, {401: self.session.refresh_session})
        return response.json()

    def _count(
        self,
        url: str = None,
        params: Dict = None,
    ):
        url = urljoin(base=self.base_url, url=url)
        if params:
            url = self.session.add_url_params(url, params)
        response = self.session.get(url)
        expanded_raise_for_status(response, {401: self.session.refresh_session})
        return response.json()["count"]

    @property
    def working_on(self):
        if self.__working_on_id:
            return self.__working_on_id
        else:
            raise Exception(
                textwrap.dedent(
                    """
						You didn't save id of entity for working on it
						Save it by calling `get(..., working_on=True)` or specify in function which you called
					"""
                )
            )

    def stop_working_on(self):
        self.__working_on_id = None

    def _download(self, url: str, target_dir: str = None):
        response = self.session.get(url, allow_redirects=True)
        filename = self.session.get_last_url_path(response.url) or "file"
        if not target_dir or not os.path.exists(target_dir):
            target_dir = "."
        full_filename = os.path.abspath(os.path.join(target_dir, filename))
        open(full_filename, "wb").write(response.content)

    def _upload_file(
        self,
        method: str,
        file_filter: Tuple[str, str],
        path: Union[os.PathLike, str],
        mime_type: str = "",
        blob_key: str = "file",
        item_id: Union[str, UUID] = "",
    ):
        url = self.base_url if not item_id else urljoin(self.base_url, f"{item_id}/")
        if re.match(file_filter[0], Path(path).name):
            response = self.session.request(
                method,
                url,
                data={},
                files={blob_key: (Path(path).name, open(path, "rb"), mime_type)},
            )
            expanded_raise_for_status(response, {401: self.session.refresh_session})
            content = response.json()
            last_renders = self.session.recent.get(self.endpoint.value, [])
            if not last_renders:
                self.session.recent[self.endpoint.value] = last_renders
            last_renders.append(content["id"])
            return content
        else:
            raise Exception(textwrap.dedent(file_filter[1]))

    def _add_object_with_title_only(
        self, endpoint: Union[str, MatlibEndpoint], title: str
    ):
        response = self.session.post(self.base_url, json={"title": title})
        expanded_raise_for_status(response, {401: self.session.refresh_session})
        content = response.json()
        last_added = self.session.recent.get(endpoint, [])
        if not last_added:
            self.session.recent[endpoint] = last_added
        last_added.append(content["id"])
        return response.json()

    def _get_workspace(
        self,
        entity: str,
        search: str = None,
        ordering: str = None,
        limit: int = None,
        offset: int = None,
    ):
        return self._get_list(
            "workspace", limit, offset, {"ordering": ordering, "search": search}
        )

    def _create(self, data: Dict[str, Any], working_on: bool = False):
        response = self.session.post(self.base_url, json=data)
        expanded_raise_for_status(response, {401: self.session.refresh_session})
        content = response.json()
        if working_on:
            self.__working_on_id = content["id"]
        return response.json()

    def _update(
        self,
        data: Dict[str, Any],
        item_id: Union[str, UUID] = "",
        working_on: bool = False,
    ):
        response = self.session.patch(
            urljoin(self.base_url, f"{item_id if item_id else self.working_on}/"),
            json=data,
        )
        expanded_raise_for_status(response, {401: self.session.refresh_session})
        content = response.json()
        if working_on:
            self.__working_on_id = content["id"]
        return response.json()

    def _post_as_action(
        self, item_id: Union[str, UUID], action: str, data: Dict[str, Any] = None
    ):
        response = self.session.post(
            urljoin(self.base_url, f"{item_id}/{action}/"), data=data
        )
        expanded_raise_for_status(response, {401: self.session.refresh_session})
        return response.json()

    def _calculate_s3_etag(self, filename, path):
        filepath = os.path.join(path, filename)
        if not os.path.exists(filepath):
            return None

        md5s = []
        with open(filepath, "rb") as fp:
            while True:
                data = fp.read(8 * 1024 * 1024)
                if not data:
                    break
                md5s.append(hashlib.md5(data))

        if len(md5s) < 1:
            return f"{hashlib.md5().hexdigest()}"
        if len(md5s) == 1:
            return f"{md5s[0].hexdigest()}"

        digests = b"".join(m.digest() for m in md5s)
        digests_md5 = hashlib.md5(digests)
        return f"{digests_md5.hexdigest()}-{len(md5s)}"


class MatlibEntityListClient(MatlibEntityClient):
    def get_list(self, limit: int = None, offset: int = None, params: dict = None):
        return self._get_list(limit=limit, offset=offset, params=params)

    def get(self, item_id: str, working_on: bool = False):
        return self._get_by_id(item_id=item_id, working_on=working_on)

    def count(self, params: dict = None):
        return super()._count(params=params)


class MatlibMaterialsClient(MatlibEntityListClient):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs, endpoint=MatlibEndpoint.MATERIALS)

    def create(self, title: str, working_on: bool = False, **kwargs):
        return super()._create(
            {"title": title, "renders": [], "packages": [], **kwargs}, working_on
        )

    def update(
        self, item_id: Union[str, UUID] = "", working_on: bool = False, **kwargs
    ):
        return super()._update({**kwargs}, item_id, working_on)

    def create_from_recent(
        self,
        title: str,
        exclusions: Dict[str, Union[str, Collection[Union[str, UUID]]]],
        clear_recent: bool = False,
        **kwargs,
    ):
        get_recent_records = lambda it: exclusions.get(
            it, self.session.recent.get(it, [])
        )
        response = self.create(
            title,
            renders=get_recent_records("renders"),
            packages=get_recent_records("packages"),
            category=self.session.recent.get("category", ""),
            tags=get_recent_records("tags"),
            **kwargs,
        )
        if clear_recent:
            self.session.clear_recent()
        return response

    def update_from_recent(
        self,
        exclusions: Dict[str, Union[str, Collection[Union[str, UUID]]]],
        item_id: Union[str, UUID] = "",
        clear_recent: bool = False,
        **kwargs,
    ):
        get_recent_records = lambda it: exclusions.get(
            it, self.session.recent.get(it, [])
        )
        response = self.update(
            item_id,
            renders=get_recent_records("renders"),
            packages=get_recent_records("packages"),
            category=self.session.recent.get("category", ""),
            tags=get_recent_records("tags"),
            **kwargs,
        )
        if clear_recent:
            self.session.clear_recent()
        return response

    def __favorite_req(self, item_id: Union[str, UUID], action: str):
        response = self.session.post(
            urljoin(self.base_url, f"{item_id}/{action}_favorite/")
        )
        expanded_raise_for_status(response, {401: self.session.refresh_session})
        return response.json()

    def add_to_favorite(self, item_id: Union[str, UUID] = ""):
        return self.__favorite_req(item_id if item_id else self.working_on, "add_to")

    def remove_from_favorite(self, item_id: Union[str, UUID] = ""):
        return self.__favorite_req(
            item_id if item_id else self.working_on, "remove_from"
        )

    def get_workspace(
        self,
        search: str = None,
        ordering: str = None,
        limit: int = None,
        offset: int = None,
    ):
        return super()._get_workspace(
            self.endpoint.value, search, ordering, limit, offset
        )

    def send_for_review(self, item_id: Union[str, UUID] = ""):
        return super()._post_as_action(
            item_id if item_id else self.working_on, "send_for_review"
        )

    def cancel_review(self, item_id: Union[str, UUID] = ""):
        return super()._post_as_action(
            item_id if item_id else self.working_on, "cancel_review"
        )


class MatlibCategoriesClient(MatlibEntityListClient):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs, endpoint=MatlibEndpoint.CATEGORIES)


class MatlibCollectionsClient(MatlibEntityListClient):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs, endpoint=MatlibEndpoint.COLLECTIONS)

    def create(self, title: str, working_on: bool = False, **kwargs):
        return super()._create({"title": title, **kwargs}, working_on)

    def update(
        self, item_id: Union[str, UUID] = "", working_on: bool = False, **kwargs
    ):
        return super()._update({**kwargs}, item_id, working_on)

    def get_workspace(
        self,
        search: str = None,
        ordering: str = None,
        limit: int = None,
        offset: int = None,
    ):
        return super()._get_workspace(
            self.endpoint.value, search, ordering, limit, offset
        )

    def get_favorite(
        self,
        search: str = None,
        ordering: str = None,
        limit: int = None,
        offset: int = None,
    ):
        return super()._get("favorite")

    def add_material(
        self, item_id: Union[str, UUID] = "", material_id: Union[str, UUID] = None
    ):
        return super()._post_as_action(
            item_id if item_id else self.working_on,
            "add_material",
            {"material_id": material_id},
        )


class MatlibTagsClient(MatlibEntityListClient):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs, endpoint=MatlibEndpoint.TAGS)

    def create(self, title: str):
        return super()._add_object_with_title_only(self.endpoint.value, title)


class MatlibRendersClient(MatlibEntityListClient):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs, endpoint=MatlibEndpoint.RENDERS)

    def download(
        self, item_id: Union[str, UUID] = "", target_dir: Union[os.PathLike, str] = None
    ):
        self._download(
            url=urljoin(
                self.base_url, f"{item_id if item_id else self.working_on}/download/"
            ),
            target_dir=target_dir,
        )

    def download_thumbnail(
        self, item_id: Union[str, UUID] = "", target_dir: Union[os.PathLike, str] = None
    ):
        self._download(
            url=urljoin(
                self.base_url,
                f"{item_id if item_id else self.working_on}/download_thumbnail/",
            ),
            target_dir=target_dir,
        )

    def upload(self, path: Union[os.PathLike, str]):
        return super()._upload_file(
            "post",
            (
                r".+.(jpg|png|jpeg)",
                """
					render image pattern 'name.<ext>' doesn't match, where
					ext - jpg, jpeg, png
				""",
            ),
            path,
            "image/jpeg",
            "image",
        )


class MatlibPackagesClient(MatlibEntityListClient):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs, endpoint=MatlibEndpoint.PACKAGES)

    def compare_etags(
        self,
        item_id: Union[str, UUID] = "",
        target_dir: Union[os.PathLike, str] = ".",
        custom_filename: str = None,
    ):
        package = super()._get_by_id(item_id=item_id)
        s3_etag = package["etag"]
        if custom_filename:
            local_etag = super()._calculate_s3_etag(custom_filename, target_dir)
        else:
            local_etag = super()._calculate_s3_etag(package["file"], target_dir)
        return s3_etag == local_etag

    def download(
        self, item_id: Union[str, UUID] = "", target_dir: Union[os.PathLike, str] = "."
    ):
        if not self.compare_etags(item_id, target_dir):
            self._download(
                url=f"{self.base_url}{item_id if item_id else self.working_on}/download/",
                target_dir=target_dir,
            )

    def upload(self, path: Union[os.PathLike, str]):
        return super()._upload_file(
            "post",
            (
                r".+_(1|2|4|8)k_(8|16|32)b.zip",
                """
					archive filename pattern 'MaterialName_<res>k_<depth>b.zip' doesn't match, where
					res: 1, 2, 4, 8
					depth: 8, 16, 32
				""",
            ),
            path,
            "application/zip",
            "file",
        )


class MatlibClient:
    def login(self, password: str, email: str = "", username: str = ""):
        self.session._login(password, email, username)

    def logout(self):
        self.session._logout()
        self.session.clear_recent()
        for entity in [
            self.materials,
            self.collections,
            self.categories,
            self.tags,
            self.renders,
            self.packages,
        ]:
            entity.stop_working_on()

    def __init__(self, host: str = "https://api.matlib.gpuopen.com/"):
        """
        MaterialX Library API Client
        :param host (str): MaterialX Library host (default: https://api.matlib.gpuopen.com/)
        """

        self.session = MatlibSession(host)
        self.materials = MatlibMaterialsClient(session=self.session)
        self.collections = MatlibCollectionsClient(session=self.session)
        self.categories = MatlibCategoriesClient(session=self.session)
        self.tags = MatlibTagsClient(session=self.session)
        self.renders = MatlibRendersClient(session=self.session)
        self.packages = MatlibPackagesClient(session=self.session)

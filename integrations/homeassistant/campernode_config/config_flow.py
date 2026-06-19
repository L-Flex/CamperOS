"""Config flow for CamperNode Config."""

from __future__ import annotations

from homeassistant import config_entries

from .const import DOMAIN


class CamperNodeConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    """Handle a config flow for CamperNode Config."""

    VERSION = 1

    async def async_step_user(self, user_input=None):
        """Add the integration — one instance per Home Assistant."""
        if self._async_current_entries():
            return self.async_abort(reason="single_instance_allowed")
        return self.async_create_entry(title="CamperNode Config", data={})
